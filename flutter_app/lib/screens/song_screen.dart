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
  bool isPlaying = false;
  String _ownerUid = '';
  String _ownerName = '';
  String _ownerSongTitle = '';

  bool get _iAmOwner => _ownerUid.isNotEmpty && _ownerUid == _myUid;
  bool get _isPlayingMine => isPlaying && _iAmOwner;
  bool get _someoneElsePlaying => isPlaying && !_iAmOwner;

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
  double _chosenSpeed = 1.0; // 0.1..2.0
  _PlayMode? _mode;

  bool _didInitHandsChoiceFromInitial = false;

  @override
  void initState() {
    super.initState();

    _selectedDifficulty = _cleanDifficulty(widget.initialDifficulty);
    final String h = _cleanHandsLabel(widget.initialHands);
    _handsChoice = (h == 'BOTH') ? _HandsChoice.twoHands : _HandsChoice.oneHand;

    _playRef = rtdb.FirebaseDatabase.instance.ref("esp32API/playCommand");
    _connectedRef = rtdb.FirebaseDatabase.instance.ref(".info/connected");

    // Connection awareness only
    _connSub = _connectedRef.onValue.listen((event) {
      // no-op; kept for future use
    });

    // Listen play status + owner fields
    _playSub = _playRef.onValue.listen((event) {
      final Map<dynamic, dynamic>? data =
          event.snapshot.value as Map<dynamic, dynamic>?;
      if (!mounted || data == null) return;

      setState(() {
        isPlaying = (data["status"] ?? "stopped") == "playing";
        _ownerUid = (data["ownerUid"] ?? "").toString();
        _ownerName = (data["ownerName"] ?? "").toString();
        _ownerSongTitle = (data["ownerSongTitle"] ?? "").toString();
      });
    });
  }

  @override
  void dispose() {
    _playSub.cancel();
    _connSub.cancel();
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
      "ownerUid": "",
      "ownerName": "",
      "ownerSongId": "",
      "ownerSongTitle": "",
      "startedAt": 0,
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

  // ---------------- RTDB commands ----------------
  Future<void> sendPlaybackCommand(bool play, String path) async {
    final rtdb.DataSnapshot snapshot = await _playRef.get();
    int count = 0;

    if (snapshot.exists) {
      final Object? raw = snapshot.value;
      if (raw is Map) {
        final Map<dynamic, dynamic> data = raw as Map<dynamic, dynamic>;
        final Object? c = data["commandsCounter"];
        count = c is int ? c : 0;
      }
    }

    await _playRef.update({
      "commandsCounter": count + 1,
      "fileToPlay": path,
      "playMode": _playModeToInt(_mode),
      "status": play ? "playing" : "stopped",
      "metronome": _metronomeOn,
      "speed": _chosenSpeed,
      // optional debug string:
      "uiMode": _mode?.name ?? "",
    });
  }

  Future<void> _clearOwnerFields() async {
    await _playRef.update({
      "ownerUid": "",
      "ownerName": "",
      "ownerSongId": "",
      "ownerSongTitle": "",
      "startedAt": 0,
    });
  }

  Future<bool> _tryStartPlayingWithLock(String path) async {
    final rtdb.TransactionResult tr =
        await _playRef.runTransaction((currentData) {
      final Map<dynamic, dynamic> data = (currentData is Map)
          ? Map<dynamic, dynamic>.from(currentData as Map)
          : <dynamic, dynamic>{};

      final String status = (data["status"] ?? "stopped").toString();
      final String ownerUid = (data["ownerUid"] ?? "").toString();

      final bool someoneElsePlaying =
          (status == "playing" && ownerUid.isNotEmpty && ownerUid != _myUid);

      if (someoneElsePlaying) {
        return rtdb.Transaction.abort();
      }

      final Object? c = data["commandsCounter"];
      final int count = c is int ? c : 0;

      data["commandsCounter"] = count + 1;
      data["fileToPlay"] = path;
      data["playMode"] = _playModeToInt(_mode);
      data["status"] = "playing";
      data["metronome"] = _metronomeOn;
      data["speed"] = _chosenSpeed;

      // owner fields
      data["ownerUid"] = _myUid;
      data["ownerName"] = _myName;
      data["ownerSongId"] = widget.songId;
      data["ownerSongTitle"] = widget.title;
      data["startedAt"] = DateTime.now().millisecondsSinceEpoch;

      // optional debug string:
      data["uiMode"] = _mode?.name ?? "";

      return rtdb.Transaction.success(data);
    });

    return tr.committed;
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
              await sendPlaybackCommand(false, _currentStoragePath);
              await _clearOwnerFields();
              await _disarmOnDisconnect();
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

  Future<bool> _showStopBeforeChangeDialog() async {
    final bool? stop = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Text("Stop playing?"),
        content: const Text(
          "The song is currently playing.\nDo you want to stop it before changing settings?",
        ),
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
            child: const Text("Stop & Change"),
          ),
        ],
      ),
    );

    return stop == true;
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

  // ---------------- speed dialog ----------------
  Future<bool> _showChooseSpeedDialog() async {
    double tempSpeed = _chosenSpeed.clamp(0.1, 2.0);

    final bool? ok = await showDialog<bool>(
      context: context,
      barrierDismissible: true,
      builder: (context) => AlertDialog(
        backgroundColor: const Color.fromARGB(255, 23, 23, 23),
        title: const Text(
          "Choose speed:",
          style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
        ),
        content: StatefulBuilder(
          builder: (context, setLocal) => Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Slider(
                value: tempSpeed,
                min: 0.1,
                max: 2.0,
                divisions: 19,
                label: tempSpeed.toStringAsFixed(1),
                onChanged: (v) {
                  final double snapped = (v * 10).round() / 10.0;
                  setLocal(() => tempSpeed = snapped);
                },
              ),
              const SizedBox(height: 6),
              Text(
                "${tempSpeed.toStringAsFixed(1)}x",
                style: const TextStyle(color: Colors.white70),
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text("Cancel", style: TextStyle(color: Colors.white)),
          ),
          TextButton(
            onPressed: () {
              setState(() => _chosenSpeed = tempSpeed);
              Navigator.pop(context, true);
            },
            child: const Text("OK", style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );

    return ok == true;
  }

  // ---------------- recompute storage path ----------------
  void _recomputeStoragePath() {
    final Map<String, dynamic> diffs = _lastDiffs;
    if (diffs.isEmpty) {
      _currentStoragePath = '';
      return;
    }

    final List<String> candidateRawDiffKeys = <String>[];
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

    String bestPath = '';

    for (final String rawDiffKey in candidateRawDiffKeys) {
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

        if (wantTwo && isTwo) {
          bestPath = p;
          break;
        }

        if (!wantTwo && !isTwo) {
          bestPath = p;
          break;
        }
      }

      if (bestPath.isNotEmpty) break;
    }

    _currentStoragePath = bestPath;
  }

  // ---------------- change settings while playing ----------------
  Future<void> _attemptChangeWhilePlaying(void Function() applyChange) async {
    if (!_isPlayingMine) {
      setState(() {
        applyChange();
        _recomputeStoragePath();
      });
      return;
    }

    final bool ok = await _showStopBeforeChangeDialog();
    if (!ok) return;

    await sendPlaybackCommand(false, _currentStoragePath);
    await _clearOwnerFields();
    await _disarmOnDisconnect();

    if (!mounted) return;

    setState(() {
      applyChange();
      _recomputeStoragePath();
    });
  }

  // ---------------- play/stop ----------------
  Future<void> _onPlayStopPressed() async {
    if (_mode == null) return;
    _recomputeStoragePath();
    final String path = _currentStoragePath;
    if (path.isEmpty) return;

    if (_someoneElsePlaying) {
      await _showSomeoneElsePlayingDialog();
      return;
    }

    if (_isPlayingMine) {
      await sendPlaybackCommand(false, path);
      await _clearOwnerFields();
      await _disarmOnDisconnect();

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
  }

  // =====================
  // NEW: 3 green circles (mode + play)
  // =====================
  Future<void> _onModeCirclePressed(_PlayMode m) async {
    // if someone else playing => just show dialog
    if (_someoneElsePlaying) {
      await _showSomeoneElsePlayingDialog();
      return;
    }

    // If I'm currently playing, keep current behavior: stop (single place to stop)
    // But we still allow changing mode for the next run (after stop)
    if (_isPlayingMine) {
      // optional: ask stop before changing mode
      final bool ok = await _showStopBeforeChangeDialog();
      if (!ok) return;

      await sendPlaybackCommand(false, _currentStoragePath);
      await _clearOwnerFields();
      await _disarmOnDisconnect();
      if (!mounted) return;
    }

    // set mode + update RTDB immediately
    if (!mounted) return;
    setState(() => _mode = m);

    // Follow mode: ask speed before play
    if (m == _PlayMode.follow) {
      final bool ok = await _showChooseSpeedDialog();
      if (!ok) return;
      if (!mounted) return;
      setState(() => _mode = _PlayMode.follow);
    } else {
      // default speed for memorize/simon (you can change if needed)
      setState(() => _chosenSpeed = 1.0);
    }

    // Update RTDB "realtime" as requested (even before play)
    await _playRef.update({
      "playMode": _playModeToInt(_mode),
    "uiMode": _mode?.name ?? "",
      "metronome": _metronomeOn,
      "speed": _chosenSpeed,
    });

    // Start playing immediately (because it's a PLAY circle)
    await _onPlayStopPressed();
  }

  // ====================== UI ======================
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

                if (!_didInitHandsChoiceFromInitial) {
                  if (initialHandsClean == 'BOTH' && handLabel == 'BOTH') {
                    initialChoiceFound = _HandsChoice.twoHands;
                  } else if ((initialHandsClean == 'RIGHT' || initialHandsClean == 'LEFT') &&
                      (handLabel == 'RIGHT' || handLabel == 'LEFT')) {
                    initialChoiceFound = _HandsChoice.oneHand;
                  }
                }
              }
            }

            final bool showHandsSelector = hasOneHand && hasTwoHands;

            if (!_didInitHandsChoiceFromInitial) {
              if (initialChoiceFound != null) {
                _handsChoice = initialChoiceFound!;
              } else {
                if (showHandsSelector) {
                  _handsChoice =
                      (initialHandsClean == 'BOTH') ? _HandsChoice.twoHands : _HandsChoice.oneHand;
                } else if (hasTwoHands && !hasOneHand) {
                  _handsChoice = _HandsChoice.twoHands;
                } else if (hasOneHand && !hasTwoHands) {
                  _handsChoice = _HandsChoice.oneHand;
                }
              }
              _didInitHandsChoiceFromInitial = true;
            }

            if (!showHandsSelector) {
              if (hasTwoHands && !hasOneHand) _handsChoice = _HandsChoice.twoHands;
              if (hasOneHand && !hasTwoHands) _handsChoice = _HandsChoice.oneHand;
            }

            _recomputeStoragePath();
            final bool canPlay = _currentStoragePath.isNotEmpty;
            final bool circlesEnabled = canPlay && !_someoneElsePlaying;

            final double screenHeight = MediaQuery.of(context).size.height;
            final double screenWidth = MediaQuery.of(context).size.width;
            final double coverHeight = screenHeight * 0.25;
            final double coverWidth = screenWidth * 0.6;

            return SingleChildScrollView(
              padding: const EdgeInsets.symmetric(horizontal: 30, vertical: 12),
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
                          style: const TextStyle(
                            color: Colors.white70,
                            fontSize: 18,
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 30),

                  if (showDifficultySelector || showHandsSelector) ...[
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        if (showDifficultySelector)
                          Expanded(
                            child: _LabeledBox(
                              label: "Difficulty:",
                              child: _DarkDropdown(
                                value: _selectedDifficulty,
                                items: diffLabels,
                                onChanged: (v) {
                                  _attemptChangeWhilePlaying(() {
                                    _selectedDifficulty = v;
                                    _didInitHandsChoiceFromInitial = false;
                                  });
                                },
                              ),
                            ),
                          ),
                        if (showDifficultySelector && showHandsSelector) const SizedBox(width: 12),
                        if (showHandsSelector)
                          Expanded(
                            child: _LabeledBox(
                              label: "Hands:",
                              child: _HandsOneVsTwoPicker(
                                value: _handsChoice,
                                onChanged: (v) {
                                  _attemptChangeWhilePlaying(() {
                                    _handsChoice = v;
                                  });
                                },
                              ),
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: 12),
                  ],

                  Row(
                    children: [
                      const SizedBox(
                        width: 110,
                        child: Text(
                          "Metronome:",
                          style: TextStyle(color: Colors.white, fontSize: 16),
                        ),
                      ),
                      Switch(
                        value: _metronomeOn,
                        onChanged: (v) {
                          _attemptChangeWhilePlaying(() {
                            _metronomeOn = v;
                          });
                        },
                        activeColor: Colors.blueAccent,
                      ),
                    ],
                  ),

                  const SizedBox(height: 18),

                  // =========================
                  // NEW: 3 green circles
                  // =========================

                  Opacity(
                    opacity: circlesEnabled ? 1.0 : 0.45,
                    child: IgnorePointer(
                      ignoring: !circlesEnabled,
                      child: _ModeCirclesRow(
                        selected: _mode,
                        isPlayingMine: _isPlayingMine,
                        onMemorize: () => _onModeCirclePressed(_PlayMode.memorize),
                        onFollow: () => _onModeCirclePressed(_PlayMode.follow),
                        onSimon: () => _onModeCirclePressed(_PlayMode.simon),
                      ),
                    ),
                  ),
                  const SizedBox(height: 14),

                  if (_someoneElsePlaying)
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: Text(
                        'Playing now: ${_ownerName.isEmpty ? "Someone" : _ownerName}',
                        style: const TextStyle(color: Colors.white70),
                        textAlign: TextAlign.center,
                      ),
                    ),

                  const SizedBox(height: 10),
                ],
              ),
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
// NEW circles widget
// =====================
class _ModeCirclesRow extends StatelessWidget {
  const _ModeCirclesRow({
    required this.selected,
    required this.isPlayingMine,
    required this.onMemorize,
    required this.onFollow,
    required this.onSimon,
  });

  final _PlayMode? selected;
  final bool isPlayingMine;

  final VoidCallback onMemorize;
  final VoidCallback onFollow;
  final VoidCallback onSimon;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Expanded(
          child: _GreenModeCircle(
            title: "Memorize\nSong",
            selected: selected == _PlayMode.memorize,
            isPlayingMine: isPlayingMine && selected == _PlayMode.memorize,
            onTap: onMemorize,
          ),
        ),
        const SizedBox(width: 14),
        Expanded(
          child: _GreenModeCircle(
            title: "Follow\nSong",
            selected: selected == _PlayMode.follow,
            isPlayingMine: isPlayingMine && selected == _PlayMode.follow,
            onTap: onFollow,
          ),
        ),
        const SizedBox(width: 14),
        Expanded(
          child: _GreenModeCircle(
            title: "Simon\nSong",
            selected: selected == _PlayMode.simon,
            isPlayingMine: isPlayingMine && selected == _PlayMode.simon,
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
  });

  final String title;
  final bool selected;
  final bool isPlayingMine;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final Color fill = selected ? const Color(0xFF00C853) : const Color(0xFF2E7D32);
    final Color border = selected ? Colors.white : Colors.transparent;

   return Center(
  child: SizedBox(
    width: 120,   // <-- גודל עיגול (תשני למה שבא לך: 110/130 וכו')
    height: 120,
    child: InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(999),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
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
              Icon(
                isPlayingMine ? Icons.stop_circle : Icons.play_circle_fill,
                color: Colors.black,
                size: 44, // <-- היה 34
              ),
              const SizedBox(height: 8),
              Text(
                title,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  color: Colors.black,
                  fontWeight: FontWeight.w900,
                  fontSize: 14, // <-- היה 12
                  height: 1.1,  // <-- קצת יותר מרווח
                ),
              ),
            ],
          ),
        ),
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

class _HandsOneVsTwoPicker extends StatelessWidget {
  final _HandsChoice value;
  final ValueChanged<_HandsChoice> onChanged;

  const _HandsOneVsTwoPicker({required this.value, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF2A2A2A),
        borderRadius: BorderRadius.circular(10),
      ),
      padding: const EdgeInsets.all(6),
      child: Row(
        children: [
          _segOneHand(
            selected: value == _HandsChoice.oneHand,
            onTap: () => onChanged(_HandsChoice.oneHand),
          ),
          const SizedBox(width: 6),
          _segTwoHands(
            selected: value == _HandsChoice.twoHands,
            onTap: () => onChanged(_HandsChoice.twoHands),
          ),
        ],
      ),
    );
  }

  Widget _segOneHand({required bool selected, required VoidCallback onTap}) {
    return Expanded(
      child: InkWell(
        borderRadius: BorderRadius.circular(8),
        onTap: onTap,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 160),
          padding: const EdgeInsets.symmetric(vertical: 10),
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(8),
            color: selected ? Colors.blueAccent.withOpacity(0.25) : null,
            border: Border.all(
              color: selected ? Colors.blueAccent : Colors.transparent,
              width: 1.2,
            ),
          ),
          child: Icon(
            Icons.pan_tool,
            color: selected ? Colors.blueAccent : Colors.white70,
            size: 20,
          ),
        ),
      ),
    );
  }

  Widget _segTwoHands({required bool selected, required VoidCallback onTap}) {
    return Expanded(
      child: InkWell(
        borderRadius: BorderRadius.circular(8),
        onTap: onTap,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 160),
          padding: const EdgeInsets.symmetric(vertical: 10),
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(8),
            color: selected ? Colors.blueAccent.withOpacity(0.25) : null,
            border: Border.all(
              color: selected ? Colors.blueAccent : Colors.transparent,
              width: 1.2,
            ),
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                Icons.pan_tool,
                color: selected ? Colors.blueAccent : Colors.white70,
                size: 18,
              ),
              const SizedBox(width: 6),
              Icon(
                Icons.pan_tool,
                color: selected ? Colors.blueAccent : Colors.white70,
                size: 18,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
