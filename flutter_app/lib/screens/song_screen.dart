import 'dart:async';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart' as rtdb;
import 'package:flutter/material.dart';

import '../widgets/footer/bottom_navigation_bar.dart';
import '../widgets/header/my_header.dart';
import '../services/stats_service.dart';

// =====================
// Modes
// =====================
enum _PlayMode { memorize, follow, simon }
enum _HandsChoice { oneHand, twoHands }

class SongScreen extends StatefulWidget {
  final String songId;
  final String title;
  final String artist;
  final String initialDifficulty;
  final String initialHands;

  const SongScreen({
    super.key,
    required this.songId,
    required this.title,
    required this.artist,
    required this.initialDifficulty,
    required this.initialHands,
  });

  @override
  State<SongScreen> createState() => _SongScreenState();
}

class _SongScreenState extends State<SongScreen> {
  // =====================
  // Firebase RTDB refs
  // =====================
  late final rtdb.DatabaseReference _playRef;
  late final rtdb.DatabaseReference _connectedRef;
  rtdb.OnDisconnect? _onDisconnect;

  late final StreamSubscription<rtdb.DatabaseEvent> _playSub;
  late final StreamSubscription<rtdb.DatabaseEvent> _connSub;

  // ----------- auth -----------
  final FirebaseAuth _auth = FirebaseAuth.instance;
  String get _myUid => _auth.currentUser!.uid;
  String get _myName => _auth.currentUser!.email ?? 'Unknown';

  // ----------- RTDB play state -----------
  bool _isPlaying = false; // status == "playing"
  bool _isStarted = true; // started == true from ESP (true means "actually started")
  String _statusMessage = "";

  String _ownerUid = '';
  String _ownerName = '';
  String _ownerSongTitle = '';

  bool get _iAmOwner => _ownerUid.isNotEmpty && _ownerUid == _myUid;

  /// "mine is running (or starting)" — since you want status to be "playing" immediately,
  /// we treat "playing + iAmOwner" as mine even if not started yet.
  bool get _isPlayingMine => _isPlaying && _iAmOwner;

  /// This is your requested "isStart": false until ESP says started=true.
  /// Loader should show while this is true.
  bool get _isStartingMine => _isPlayingMine && !_isStarted;

  /// someone else holds the piano if they are owner and status is playing (regardless started)
  bool get _someoneElseUsingPiano =>
      (_ownerUid.isNotEmpty && _ownerUid != _myUid) && _isPlaying;

  // ----------- navigation / pop -----------
  bool _canPop = false;
  int? _pendingNavIndex;

  // ----------- song selection -----------
  late String _selectedDifficulty;
  _HandsChoice _handsChoice = _HandsChoice.oneHand;

  // Keep latest Firestore difficulties so we can recompute storage path on-demand.
  Map<String, dynamic> _lastDiffs = <String, dynamic>{};
  String _currentStoragePath = '';

  // ----------- options -----------
  bool _metronomeOn = false;
  double _chosenSpeed = 1.0; // 0.1..2.0 step 0.1
  int _segments = 1; // 1..4
  _PlayMode? _mode; // selected mode for circles

  bool _didInitHandsChoiceFromInitial = false;
  bool _userPickedHands = false;

  // if ESP never flips started=true, we can timeout and stop
  Timer? _startTimeout;

  @override
  void initState() {
    super.initState();

    _selectedDifficulty = _cleanDifficulty(widget.initialDifficulty);
    final String h = _cleanHandsLabel(widget.initialHands);
    _handsChoice = (h == 'BOTH') ? _HandsChoice.twoHands : _HandsChoice.oneHand;

    _playRef = rtdb.FirebaseDatabase.instance.ref("esp32API/playCommand");
    _connectedRef = rtdb.FirebaseDatabase.instance.ref(".info/connected");

    _connSub = _connectedRef.onValue.listen((event) {});

    // Listen play status + owner fields + started flag from ESP
    _playSub = _playRef.onValue.listen((event) {
      final Map<dynamic, dynamic>? data =
          event.snapshot.value as Map<dynamic, dynamic>?;
      if (!mounted || data == null) return;

      final String status = (data["status"] ?? "stopped").toString();
      final bool playing = status == "playing";

      // App sets started=false when sending song, ESP sets started=true when it REALLY starts.
      final bool started = (data["started"] as bool?) ?? true;

      final String msg = (data["statusMessage"] ?? "").toString();
      final String ownerUid = (data["ownerUid"] ?? "").toString();

      setState(() {
        _isPlaying = playing;
        _isStarted = started;
        _statusMessage = msg;

        _ownerUid = ownerUid;
        _ownerName = (data["ownerName"] ?? "").toString();
        _ownerSongTitle = (data["ownerSongTitle"] ?? "").toString();

        final bool someoneElseIsOwner =
            ownerUid.isNotEmpty && ownerUid != _myUid;

        if (someoneElseIsOwner && _mode != null) {
          _mode = null;
        }
      });

      // cancel timeout if song actually started or stopped
      if (!playing || started) {
        _startTimeout?.cancel();
      }
    });
  }

  @override
  void dispose() {
    _playSub.cancel();
    _connSub.cancel();
    _startTimeout?.cancel();
    super.dispose();
  }

  // ================= OnDisconnect (ONLY for owner) =================
  Future<void> _armOnDisconnectIfConnected() async {
    final rtdb.DataSnapshot snap = await _connectedRef.get();
    final bool connected = (snap.value as bool?) ?? false;
    if (!connected) return;

    _onDisconnect ??= _playRef.onDisconnect();
    await _onDisconnect!.update({
      "status": "stopped",
      "started": true,
      "ownerUid": "",
      "ownerName": "",
      "ownerSongId": "",
      "ownerSongTitle": "",
      "startedAt": 0,
      "statusMessage": "",
    });
  }

  Future<void> _disarmOnDisconnect() async {
    await _onDisconnect?.cancel();
    _onDisconnect = null;
  }

  // ---------------- helpers ----------------
  bool _isUnknownValue(String v) => v.trim().toUpperCase() == 'UNKNOWN';

  String _cleanDifficulty(dynamic raw) {
    final String u = (raw ?? '').toString().trim().toUpperCase();

    if (u.contains('SLOW') && u.contains('BEGINNER')) return 'SLOW BEGINNER';
    if (u.contains('SLOW') && u.contains('EASY')) return 'SLOW EASY';

    if (u.contains('BEGINNER')) return 'BEGINNER';
    if (u.contains('EASY')) return 'EASY';
    if (u.contains('INTERMEDIATE')) return 'INTERMEDIATE';
    if (u.contains('MEDIUM')) return 'MEDIUM';
    if (u.contains('HARD')) return 'HARD';
    if (u.contains('ADVANCED')) return 'ADVANCED';

    return 'UNKNOWN';
  }

  String _cleanHandsLabel(dynamic raw) {
    final String u = (raw ?? '')
        .toString()
        .trim()
        .toUpperCase()
        .replaceAll(' ', '')
        .replaceAll('_', '');

    if (u == 'B' ||
        u == 'BOTH' ||
        u.contains('BOTH') ||
        u.contains('TWOHANDS') ||
        u.contains('BOTHHANDS') ||
        u == 'LR' ||
        u.contains('LEFTRIGHT') ||
        u.contains('L+R') ||
        u.contains('L&R')) {
      return 'BOTH';
    }
    if (u == 'L' || u == 'LH' || u.contains('LEFT')) return 'LEFT';
    if (u == 'R' || u == 'RH' || u.contains('RIGHT')) return 'RIGHT';
    return 'UNKNOWN';
  }

  int _playModeToInt(_PlayMode? m) {
    if (m == null) return -1;
    if (m == _PlayMode.follow) return 0;
    if (m == _PlayMode.memorize) return 1;
    return 2; // simon
  }

  // --------- NEW: global hands check (minimal change) ----------
  bool _songHasOneHand(Map<String, dynamic> diffs) {
    for (final entry in diffs.entries) {
      final Map<String, dynamic> diffObj =
          entry.value as Map<String, dynamic>? ?? <String, dynamic>{};
      final Map<String, dynamic> handsObj =
          diffObj['hands'] as Map<String, dynamic>? ?? <String, dynamic>{};

      for (final h in handsObj.entries) {
        final String handLabel = _cleanHandsLabel(h.key.toString());
        if (_isUnknownValue(handLabel)) continue;
        if (handLabel == 'LEFT' || handLabel == 'RIGHT') return true;
      }
    }
    return false;
  }

  bool _songHasTwoHands(Map<String, dynamic> diffs) {
    for (final entry in diffs.entries) {
      final Map<String, dynamic> diffObj =
          entry.value as Map<String, dynamic>? ?? <String, dynamic>{};
      final Map<String, dynamic> handsObj =
          diffObj['hands'] as Map<String, dynamic>? ?? <String, dynamic>{};

      for (final h in handsObj.entries) {
        final String handLabel = _cleanHandsLabel(h.key.toString());
        if (_isUnknownValue(handLabel)) continue;
        if (handLabel == 'BOTH') return true;
      }
    }
    return false;
  }

  // ---------------- recompute storage path ----------------
  // MINIMAL FIX: if no path found in selected difficulty, fallback to ANY difficulty.
  void _recomputeStoragePath() {
    final Map<String, dynamic> diffs = _lastDiffs;
    if (diffs.isEmpty) {
      _currentStoragePath = '';
      return;
    }

    List<String> candidateRawDiffKeys = <String>[];

    // 1) try selected difficulty first (as before)
    for (final entry in diffs.entries) {
      final String rawKey = entry.key.toString();
      final String label = _cleanDifficulty(rawKey);

      if (_selectedDifficulty == 'UNKNOWN') {
        candidateRawDiffKeys.add(rawKey);
      } else {
        if (_isUnknownValue(label)) continue;
        if (label == _selectedDifficulty) candidateRawDiffKeys.add(rawKey);
      }
    }

    String bestPath = _findPathInRawDiffKeys(diffs, candidateRawDiffKeys);

    // 2) fallback: if not found, search all difficulties
    if (bestPath.isEmpty) {
      candidateRawDiffKeys = diffs.keys.map((e) => e.toString()).toList();
      bestPath = _findPathInRawDiffKeys(diffs, candidateRawDiffKeys);
    }

    _currentStoragePath = bestPath;
  }

  String _findPathInRawDiffKeys(
      Map<String, dynamic> diffs, List<String> rawDiffKeys) {
    for (final String rawDiffKey in rawDiffKeys) {
      final Map<String, dynamic> diffObj =
          diffs[rawDiffKey] as Map<String, dynamic>? ?? <String, dynamic>{};
      final Map<String, dynamic> handsObj =
          diffObj['hands'] as Map<String, dynamic>? ?? <String, dynamic>{};

      for (final entry in handsObj.entries) {
        final String rawHandKey = entry.key.toString();
        final String handLabel = _cleanHandsLabel(rawHandKey);
        if (_isUnknownValue(handLabel)) continue;

        final Map<String, dynamic> handObj =
            entry.value as Map<String, dynamic>? ?? <String, dynamic>{};
        final String p = (handObj['storagePath'] as String?) ?? '';
        if (p.isEmpty) continue;

        final bool isTwo = (handLabel == 'BOTH');
        final bool wantTwo = (_handsChoice == _HandsChoice.twoHands);

        if (wantTwo && isTwo) return p;
        if (!wantTwo && !isTwo) return p;
      }
    }
    return '';
  }

  // ===================== START/STOP with lock =====================

  /// Start: status becomes "playing" immediately + started=false
  /// ESP must flip started=true when audio really begins.
  Future<bool> _tryStartPlayingWithLock(String path) async {
    final rtdb.TransactionResult tr =
        await _playRef.runTransaction((currentData) {
      final Map<dynamic, dynamic> data = (currentData is Map)
          ? Map<dynamic, dynamic>.from(currentData as Map)
          : <dynamic, dynamic>{};

      final String status = (data["status"] ?? "stopped").toString();
      final String ownerUid = (data["ownerUid"] ?? "").toString();

      final bool someoneElsePlaying =
          (status == "playing") && ownerUid.isNotEmpty && ownerUid != _myUid;

      if (someoneElsePlaying) {
        return rtdb.Transaction.abort();
      }

      final Object? c = data["commandsCounter"];
      final int count = c is int ? c : 0;

      data["commandsCounter"] = count + 1;
      data["fileToPlay"] = path;
      data["playMode"] = _playModeToInt(_mode);

      data["status"] = "playing";
      data["started"] = false;

      data["statusMessage"] = "Loading...";
      data["metronome"] = _metronomeOn;
      data["speed"] = _chosenSpeed;
      data["segments"] = _segments;

      data["ownerUid"] = _myUid;
      data["ownerName"] = _myName;
      data["ownerSongId"] = widget.songId;
      data["ownerSongTitle"] = widget.title;
      data["startedAt"] = DateTime.now().millisecondsSinceEpoch;

      data["uiMode"] = _mode?.name ?? "";

      return rtdb.Transaction.success(data);
    });

    return tr.committed;
  }

  Future<void> _stopPlaybackAndResetUI({bool clearMode = true}) async {
    await _playRef.update({
      "status": "stopped",
      "started": true,
      "statusMessage": "",
      "playMode": clearMode ? -1 : _playModeToInt(_mode),
      "uiMode": clearMode ? "" : (_mode?.name ?? ""),
      "fileToPlay": _currentStoragePath,
      "metronome": _metronomeOn,
      "speed": _chosenSpeed,
      "segments": _segments,
    });

    await _playRef.update({
      "ownerUid": "",
      "ownerName": "",
      "ownerSongId": "",
      "ownerSongTitle": "",
      "startedAt": 0,
    });

    await _disarmOnDisconnect();
    _startTimeout?.cancel();

    if (mounted) {
      setState(() {
        _statusMessage = "";
        if (clearMode) _mode = null;
      });
    }
  }

  // ---------------- dialogs ----------------
  Future<void> _showSomeoneElsePlayingDialog() async {
    final String name = _ownerName.isNotEmpty ? _ownerName : "Someone";
    final String song = _ownerSongTitle.isNotEmpty ? _ownerSongTitle : "a song";
    if (!mounted) return;

    await showDialog<void>(
      context: context,
      builder: (_) => AlertDialog(
        backgroundColor: const Color.fromARGB(255, 23, 23, 23),
        title: const Text(
          'Song is playing',
          style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
        ),
        content: Text(
          '$name is currently playing "$song".\nPlease try again later.',
          style: const TextStyle(color: Colors.white70),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('OK', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );
  }

  Future<bool> _showStopSongDialog() async {
    final bool? stop = await showDialog<bool>(
      context: context,
      barrierDismissible: true,
      builder: (context) => AlertDialog(
        title: const Text("Stop playing?"),
        content: const Text("Do you want to stop the song?"),
        backgroundColor: const Color.fromARGB(255, 23, 23, 23),
        contentTextStyle: const TextStyle(color: Colors.white),
        titleTextStyle: const TextStyle(
          color: Colors.white,
          fontSize: 25,
          fontWeight: FontWeight.bold,
        ),
        actions: [
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () => Navigator.pop(context, false),
            child: const Text("Cancel"),
          ),
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () => Navigator.pop(context, true),
            child: const Text("Stop"),
          ),
        ],
      ),
    );

    return stop == true;
  }

  Future<void> _showExitDialog() async {
    final bool? shouldLeave = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Text("Stop playing?"),
        content: const Text("Do you want to stop the song before leaving?"),
        backgroundColor: const Color.fromARGB(255, 23, 23, 23),
        contentTextStyle: const TextStyle(color: Colors.white),
        titleTextStyle: const TextStyle(
          color: Colors.white,
          fontSize: 25,
          fontWeight: FontWeight.bold,
        ),
        actions: [
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () => Navigator.pop(context, false),
            child: const Text("Stay"),
          ),
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () async {
              await _stopPlaybackAndResetUI(clearMode: true);
              if (context.mounted) Navigator.pop(context, true);
            },
            child: const Text("Stop & Leave"),
          ),
        ],
      ),
    );

    if (shouldLeave == true) {
      if (_pendingNavIndex != null) {
        _navigateToTab(_pendingNavIndex!);
        return;
      }
      setState(() => _canPop = true);
      Navigator.of(context).pop();
    }
  }

  // ---------------- navigation ----------------
  Future<void> _handleNavLeave(int index) async {
    if (_isPlayingMine) {
      _pendingNavIndex = index;
      await _showExitDialog();
    } else {
      _navigateToTab(index);
    }
  }

  void _navigateToTab(int index) {
    if (!mounted) return;

    if (index == 0) {
      Navigator.pushReplacementNamed(context, "/search");
    } else if (index == 1) {
      Navigator.pushReplacementNamed(context, "/upload");
    } else if (index == 2) {
      Navigator.pushReplacementNamed(context, "/user");
    }
  }

  // =====================
  // PLAY/STOP (called from circle)
  // =====================
  Future<void> _startOrStop() async {
    if (_mode == null) return;

    _recomputeStoragePath();
    final String path = _currentStoragePath;
    if (path.isEmpty) return;

    if (_someoneElseUsingPiano) {
      await _showSomeoneElsePlayingDialog();
      return;
    }

    if (_isPlayingMine) {
      final bool stop = await _showStopSongDialog();
      if (!stop) return;

      await _stopPlaybackAndResetUI(clearMode: true);

      final User? u = FirebaseAuth.instance.currentUser;
      if (u != null) {
        await StatsService(FirebaseFirestore.instance).registerPracticeDay(u.uid);
      }
      return;
    }

    final bool ok = await _tryStartPlayingWithLock(path);
    if (!ok) {
      await _showSomeoneElsePlayingDialog();
      return;
    }

    final User? u = FirebaseAuth.instance.currentUser;
    if (u != null) {
      await StatsService(FirebaseFirestore.instance).onStartSong(
        uid: u.uid,
        songId: widget.songId,
      );
    }

    await _armOnDisconnectIfConnected();

    _startTimeout?.cancel();
    _startTimeout = Timer(const Duration(seconds: 20), () async {
      if (!mounted) return;

      if (_isStartingMine) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("The piano didn't start (timeout).")),
        );
        await _stopPlaybackAndResetUI(clearMode: true);
      }
    });
  }

  // Circle pressed: choose mode and start/stop
  Future<void> _onModeCirclePressed(_PlayMode m) async {
    if (_someoneElseUsingPiano) return;

    if (_isPlayingMine && _mode == m) {
      await _startOrStop();
      return;
    }

    if (_isPlayingMine && _mode != m) {
      final bool stop = await _showStopSongDialog();
      if (!stop) return;
      await _stopPlaybackAndResetUI(clearMode: true);
    }

    if (!mounted) return;

    setState(() {
      _mode = m;
    });

    await _playRef.update({
      "playMode": _playModeToInt(_mode),
      "uiMode": _mode?.name ?? "",
      "metronome": _metronomeOn,
      "speed": _chosenSpeed,
      "segments": _segments,
    });

    await _startOrStop();
  }

  // ======================
  // UI
  // ======================
  @override
  Widget build(BuildContext context) {
    final double topPad = MediaQuery.of(context).padding.top;

    return PopScope(
      canPop: _canPop,
      onPopInvoked: (didPop) {
        if (didPop) return;

        if (_isPlayingMine) {
          _showExitDialog();
        } else {
          setState(() => _canPop = true);
          Navigator.pop(context);
        }
      },
      child: Scaffold(
        backgroundColor: const Color(0xFF1E1E1E),
        appBar: PreferredSize(
          preferredSize: Size.fromHeight(60 + topPad),
          child: Padding(
            padding: EdgeInsets.only(top: topPad),
            child: MyHeader(
              title: "Song",
              isBackButton: true,
              onBack: () async {
                if (_isPlayingMine) {
                  await _showExitDialog();
                } else {
                  Navigator.pop(context);
                }
              },
            ),
          ),
        ),
        body: StreamBuilder<DocumentSnapshot>(
          stream: FirebaseFirestore.instance
              .collection('songsNEW_midi')
              .doc(widget.songId)
              .snapshots(),
          builder: (context, snap) {
            if (!snap.hasData) {
              return const Center(child: CircularProgressIndicator());
            }

            final Map<String, dynamic> data =
                snap.data!.data() as Map<String, dynamic>? ?? <String, dynamic>{};
            final Map<String, dynamic> diffs =
                data['difficulties'] as Map<String, dynamic>? ?? <String, dynamic>{};

            _lastDiffs = diffs;

            // Build map: label -> raw keys
            final Map<String, List<String>> rawKeysByDiffLabel = <String, List<String>>{};
            final List<String> unknownRawDiffKeys = <String>[];

            for (final entry in diffs.entries) {
              final String rawKey = entry.key.toString();
              final String label = _cleanDifficulty(rawKey);

              if (_isUnknownValue(label)) {
                unknownRawDiffKeys.add(rawKey);
              } else {
                rawKeysByDiffLabel.putIfAbsent(label, () => <String>[]);
                rawKeysByDiffLabel[label]!.add(rawKey);
              }
            }

            final List<String> diffLabels = rawKeysByDiffLabel.keys.toList()..sort();
            if (diffLabels.isEmpty && unknownRawDiffKeys.isNotEmpty) {
              rawKeysByDiffLabel['UNKNOWN'] = unknownRawDiffKeys;
              diffLabels.add('UNKNOWN');
            }

            final bool showDifficultySelector = diffLabels.length > 1;

            if (diffLabels.isNotEmpty) {
              if (!diffLabels.contains(_selectedDifficulty)) {
                _selectedDifficulty = diffLabels.first;
              }
            } else {
              _selectedDifficulty = 'UNKNOWN';
            }

            final List<String> selectedRawDiffKeys =
                rawKeysByDiffLabel[_selectedDifficulty] ?? <String>[];

            bool hasOneHand = false;
            bool hasTwoHands = false;

            _HandsChoice? initialChoiceFound;
            final String initialHandsClean = _cleanHandsLabel(widget.initialHands);

            for (final String rawDiffKey in selectedRawDiffKeys) {
              final Map<String, dynamic> diffObj =
                  diffs[rawDiffKey] as Map<String, dynamic>? ?? <String, dynamic>{};
              final Map<String, dynamic> handsObj =
                  diffObj['hands'] as Map<String, dynamic>? ?? <String, dynamic>{};

              for (final entry in handsObj.entries) {
                final String rawHandKey = entry.key.toString();
                final String handLabel = _cleanHandsLabel(rawHandKey);
                if (_isUnknownValue(handLabel)) continue;

                if (handLabel == 'BOTH') {
                  hasTwoHands = true;
                } else if (handLabel == 'LEFT' || handLabel == 'RIGHT') {
                  hasOneHand = true;
                }

                if (!_userPickedHands && !_didInitHandsChoiceFromInitial) {
                  if (initialHandsClean == 'BOTH' && handLabel == 'BOTH') {
                    initialChoiceFound = _HandsChoice.twoHands;
                  } else if ((initialHandsClean == 'RIGHT' || initialHandsClean == 'LEFT') &&
                      (handLabel == 'RIGHT' || handLabel == 'LEFT')) {
                    initialChoiceFound = _HandsChoice.oneHand;
                  }
                }
              }
            }

            // MINIMAL FIX: show hands selector based on whole song (not only selected difficulty)
            final bool showHandsSelector =
                _songHasOneHand(diffs) && _songHasTwoHands(diffs);

            if (!_userPickedHands) {
              if (!_didInitHandsChoiceFromInitial) {
                if (initialChoiceFound != null) {
                  _handsChoice = initialChoiceFound!;
                } else {
                  if (showHandsSelector) {
                    _handsChoice = (initialHandsClean == 'BOTH')
                        ? _HandsChoice.twoHands
                        : _HandsChoice.oneHand;
                  } else if (_songHasTwoHands(diffs) && !_songHasOneHand(diffs)) {
                    _handsChoice = _HandsChoice.twoHands;
                  } else if (_songHasOneHand(diffs) && !_songHasTwoHands(diffs)) {
                    _handsChoice = _HandsChoice.oneHand;
                  }
                }
                _didInitHandsChoiceFromInitial = true;
              }
            }

            if (!_userPickedHands && !showHandsSelector) {
              if (_songHasTwoHands(diffs) && !_songHasOneHand(diffs)) {
                _handsChoice = _HandsChoice.twoHands;
              }
              if (_songHasOneHand(diffs) && !_songHasTwoHands(diffs)) {
                _handsChoice = _HandsChoice.oneHand;
              }
            }

            _recomputeStoragePath();
            final bool canPlay = _currentStoragePath.isNotEmpty;
            final bool circlesEnabled = canPlay && !_someoneElseUsingPiano;

            final double screenHeight = MediaQuery.of(context).size.height;
            final double screenWidth = MediaQuery.of(context).size.width;
            final double coverHeight = screenHeight * 0.25;
            final double coverWidth = screenWidth * 0.6;

            final bool isStart = _isStarted;
            final bool isLoadingMine = _isPlayingMine && !isStart;

            return Stack(
              children: [
                SingleChildScrollView(
                  padding: const EdgeInsets.symmetric(horizontal: 26, vertical: 12),
                  child: Column(
                    children: [
                      Container(
                        height: coverHeight,
                        width: coverWidth,
                        decoration: BoxDecoration(
                          color: const Color(0xFFFFD54F),
                          borderRadius: BorderRadius.circular(10),
                        ),
                        child: const Center(
                          child: Icon(Icons.music_note, size: 112, color: Colors.black),
                        ),
                      ),
                      const SizedBox(height: 20),
                      Center(
                        child: Column(
                          children: [
                            Text(
                              widget.title,
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                color: Colors.white,
                                fontSize: 26,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                            const SizedBox(height: 8),
                            Text(
                              widget.artist,
                              textAlign: TextAlign.center,
                              style: const TextStyle(color: Colors.white70, fontSize: 18),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 22),

                      if (showDifficultySelector) ...[
                        _LabeledBox(
                          label: "Difficulty:",
                          child: _DarkDropdown(
                            value: _selectedDifficulty,
                            items: diffLabels,
                            onChanged: (v) {
                              setState(() {
                                _selectedDifficulty = v;
                                _didInitHandsChoiceFromInitial = false;
                                _userPickedHands = false;
                              });
                              _recomputeStoragePath();
                            },
                          ),
                        ),
                        const SizedBox(height: 14),
                      ],

                      _SettingsRow(
                        speed: _chosenSpeed,
                        showHands: showHandsSelector,
                        hands: _handsChoice,
                        metronomeOn: _metronomeOn,
                        segments: _segments,
                        onSpeedTap: () {},
                        onHandsTap: showHandsSelector
                            ? () async {
                                if (_isPlayingMine) {
                                  final bool stop = await _showStopSongDialog();
                                  if (!stop) return;
                                  await _stopPlaybackAndResetUI(clearMode: false);
                                }

                                setState(() {
                                  _userPickedHands = true;
                                  _handsChoice = (_handsChoice == _HandsChoice.oneHand)
                                      ? _HandsChoice.twoHands
                                      : _HandsChoice.oneHand;
                                });

                                _recomputeStoragePath();
                              }
                            : null,
                        onMetronomeTap: () {},
                        onSegmentsTap: () {},
                      ),

                      const SizedBox(height: 55),

                      Opacity(
                        opacity: circlesEnabled ? 1.0 : 0.45,
                        child: IgnorePointer(
                          ignoring: !circlesEnabled,
                          child: _ModeCirclesRow(
                            selected: _mode,
                            isPlayingMine: _isPlayingMine,
                            isLoadingMine: isLoadingMine,
                            onMemorize: () => _onModeCirclePressed(_PlayMode.memorize),
                            onFollow: () => _onModeCirclePressed(_PlayMode.follow),
                            onSimon: () => _onModeCirclePressed(_PlayMode.simon),
                          ),
                        ),
                      ),

                      const SizedBox(height: 12),
                      if (_someoneElseUsingPiano)
                        Padding(
                          padding: const EdgeInsets.only(top: 6),
                          child: Text(
                            'Playing now: ${_ownerName.isEmpty ? "Someone" : _ownerName}',
                            style: const TextStyle(color: Colors.white70),
                            textAlign: TextAlign.center,
                          ),
                        ),

                      if (isLoadingMine)
                        const Padding(
                          padding: EdgeInsets.only(top: 8),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Text(
                                "Loading...",
                                style: TextStyle(color: Colors.white54),
                              ),
                              SizedBox(width: 10),
                              SizedBox(
                                width: 18,
                                height: 18,
                                child: CircularProgressIndicator(strokeWidth: 2.5),
                              ),
                            ],
                          ),
                        ),

                      const SizedBox(height: 8),
                    ],
                  ),
                ),
              ],
            );
          },
        ),
        bottomNavigationBar: MyBottomNavigationBar(
          currentIndex: 0,
          onTap: _handleNavLeave,
        ),
      ),
    );
  }
}

// =====================
// Settings row widget (unchanged)
// =====================
class _SettingsRow extends StatelessWidget {
  const _SettingsRow({
    required this.speed,
    required this.showHands,
    required this.hands,
    required this.metronomeOn,
    required this.segments,
    required this.onSpeedTap,
    required this.onHandsTap,
    required this.onMetronomeTap,
    required this.onSegmentsTap,
  });

  final double speed;
  final bool showHands;
  final _HandsChoice hands;
  final bool metronomeOn;
  final int segments;

  final VoidCallback onSpeedTap;
  final VoidCallback? onHandsTap;
  final VoidCallback onMetronomeTap;
  final VoidCallback onSegmentsTap;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: _SettingPill(
            title: "Speed",
            valueText: "${speed.toStringAsFixed(1)}x",
            icon: Icons.speed,
            onTap: onSpeedTap,
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: _SettingPill(
            title: "Hands",
            valueText: showHands ? " " : "—",
            icon: Icons.pan_tool,
            onTap: showHands ? onHandsTap : null,
            disabled: !showHands,
            customIcon:
                showHands ? _HandsIcon(twoHands: hands == _HandsChoice.twoHands) : null,
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: _SettingPill(
            title: "Metronome",
            valueText: metronomeOn ? "On" : "Off",
            icon: Icons.music_note,
            onTap: onMetronomeTap,
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: _SettingPill(
            title: "segments",
            valueText: "$segments",
            icon: Icons.view_week,
            onTap: onSegmentsTap,
          ),
        ),
      ],
    );
  }
}

class _HandsIcon extends StatelessWidget {
  const _HandsIcon({required this.twoHands});
  final bool twoHands;

  @override
  Widget build(BuildContext context) {
    const IconData handIcon = Icons.front_hand_rounded;

    if (!twoHands) {
      return const Icon(handIcon, color: Colors.white, size: 22);
    }

    return const Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(handIcon, color: Colors.white, size: 18),
        SizedBox(width: 4),
        Icon(handIcon, color: Colors.white, size: 18),
      ],
    );
  }
}

class _SettingPill extends StatelessWidget {
  const _SettingPill({
    required this.title,
    required this.valueText,
    required this.icon,
    required this.onTap,
    this.disabled = false,
    this.showX = false,
    this.customIcon,
  });

  final String title;
  final String valueText;
  final IconData icon;
  final VoidCallback? onTap;
  final bool disabled;
  final bool showX;
  final Widget? customIcon;

  @override
  Widget build(BuildContext context) {
    final bool enabled = !disabled && onTap != null;

    return Opacity(
      opacity: enabled ? 1.0 : 0.45,
      child: InkWell(
        onTap: enabled ? onTap : null,
        borderRadius: BorderRadius.circular(14),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 9),
          decoration: BoxDecoration(
            color: const Color(0xFF2A2A2A),
            borderRadius: BorderRadius.circular(14),
            border: Border.all(color: Colors.white12),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Stack(
                alignment: Alignment.center,
                clipBehavior: Clip.none,
                children: [
                  SizedBox(
                    height: 22,
                    child: Center(
                      child: customIcon ?? Icon(icon, color: Colors.white, size: 22),
                    ),
                  ),
                  if (showX)
                    Positioned(
                      right: -2,
                      top: -2,
                      child: Container(
                        padding: const EdgeInsets.all(2),
                        decoration: const BoxDecoration(
                          shape: BoxShape.circle,
                          color: Colors.redAccent,
                        ),
                        child: const Icon(
                          Icons.close,
                          size: 12,
                          color: Colors.white,
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 6),
              Text(
                title,
                style: const TextStyle(color: Colors.white70, fontSize: 12),
                overflow: TextOverflow.ellipsis,
              ),
              const SizedBox(height: 2),
              Text(
                valueText,
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 13,
                  fontWeight: FontWeight.w800,
                ),
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// =====================
// Circles widget (loader logic isStart/isLoadingMine)
// =====================
class _ModeCirclesRow extends StatelessWidget {
  const _ModeCirclesRow({
    required this.selected,
    required this.isPlayingMine,
    required this.isLoadingMine,
    required this.onMemorize,
    required this.onFollow,
    required this.onSimon,
  });

  final _PlayMode? selected;
  final bool isPlayingMine;

  /// true while my "started" is false
  final bool isLoadingMine;

  final VoidCallback onMemorize;
  final VoidCallback onFollow;
  final VoidCallback onSimon;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: _GreenModeCircle(
            title: "Interactive\nSong",
            selected: selected == _PlayMode.memorize,
            isPlayingMine: isPlayingMine && selected == _PlayMode.memorize,
            isLoading: isLoadingMine && selected == _PlayMode.memorize,
            onTap: onMemorize,
          ),
        ),
        const SizedBox(width: 14),
        Expanded(
          child: _GreenModeCircle(
            title: "Follow\nSong",
            selected: selected == _PlayMode.follow,
            isPlayingMine: isPlayingMine && selected == _PlayMode.follow,
            isLoading: isLoadingMine && selected == _PlayMode.follow,
            onTap: onFollow,
          ),
        ),
        const SizedBox(width: 14),
        Expanded(
          child: _GreenModeCircle(
            title: "Simon\nGame",
            selected: selected == _PlayMode.simon,
            isPlayingMine: isPlayingMine && selected == _PlayMode.simon,
            isLoading: isLoadingMine && selected == _PlayMode.simon,
            onTap: onSimon,
          ),
        ),
      ],
    );
  }
}

class _GreenModeCircle extends StatelessWidget {
  const _GreenModeCircle({
    required this.title,
    required this.selected,
    required this.isPlayingMine,
    required this.onTap,
    required this.isLoading,
  });

  final String title;
  final bool selected;
  final bool isPlayingMine;
  final VoidCallback onTap;
  final bool isLoading;

  @override
  Widget build(BuildContext context) {
    final Color fill =
        selected ? const Color(0xFF00C853) : const Color(0xFF2E7D32);
    final Color border = selected ? Colors.white : Colors.transparent;

    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(999),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
        height: 118,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: fill,
          border: Border.all(color: border, width: 2),
          boxShadow: [
            BoxShadow(
              blurRadius: 18,
              offset: const Offset(0, 10),
              color: Colors.black.withOpacity(0.35),
            )
          ],
        ),
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Stack(
                alignment: Alignment.center,
                children: [
                  Icon(
                    isPlayingMine ? Icons.stop_circle : Icons.play_circle_fill,
                    color: Colors.black,
                    size: 42,
                  ),
                  if (isLoading)
                    const SizedBox(
                      width: 34,
                      height: 34,
                      child: CircularProgressIndicator(strokeWidth: 3),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              Text(
                title,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  color: Colors.black,
                  fontWeight: FontWeight.w900,
                  fontSize: 14,
                  height: 1.05,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ---------- small widgets ----------
class _LabeledBox extends StatelessWidget {
  final String label;
  final Widget child;

  const _LabeledBox({required this.label, required this.child});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [child],
    );
  }
}

class _DarkDropdown extends StatelessWidget {
  final String value;
  final List<String> items;
  final ValueChanged<String> onChanged;

  const _DarkDropdown({
    required this.value,
    required this.items,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: const Color(0xFF2A2A2A),
        borderRadius: BorderRadius.circular(10),
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          value: value,
          dropdownColor: const Color(0xFF2A2A2A),
          isExpanded: true,
          iconEnabledColor: Colors.white70,
          items: items
              .map(
                (d) => DropdownMenuItem<String>(
                  value: d,
                  child: Text(
                    d,
                    style: const TextStyle(color: Colors.white),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              )
              .toList(),
          onChanged: (v) {
            if (v == null) return;
            onChanged(v);
          },
        ),
      ),
    );
  }
}
