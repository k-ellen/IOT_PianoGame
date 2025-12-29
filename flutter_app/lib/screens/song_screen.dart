import 'dart:async';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

import '../widgets/footer/bottom_navigation_bar.dart';
import '../widgets/header/my_header.dart';

enum _PlayMode { memorize, follow }

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
  bool isPlaying = false;

  bool _canPop = false;
  int? _pendingNavIndex;

  late final DatabaseReference ref;
  late final StreamSubscription<DatabaseEvent> subscription;

  late String _selectedDifficulty;
  _HandsChoice _handsChoice = _HandsChoice.oneHand;

  // Keep latest Firestore difficulties so we can recompute storage path on-demand.
  Map<String, dynamic> _lastDiffs = {};

  String _currentStoragePath = '';

  bool _metronomeOn = false;
  double _chosenSpeed = 1.0; // 0.1..2.0

  _PlayMode _mode = _PlayMode.memorize;

  bool _didInitHandsChoiceFromInitial = false;

  @override
  void initState() {
    super.initState();

    _selectedDifficulty = _cleanDifficulty(widget.initialDifficulty);

    final String h = _cleanHandsLabel(widget.initialHands);
    _handsChoice = (h == 'BOTH') ? _HandsChoice.twoHands : _HandsChoice.oneHand;

    ref = FirebaseDatabase.instance.ref("esp32API/playCommand");

    subscription = ref.onValue.listen((event) {
      final Map<dynamic, dynamic>? data =
          event.snapshot.value as Map<dynamic, dynamic>?;

      if (!mounted || data == null) return;

      setState(() {
        isPlaying = (data["status"] ?? "stopped") == "playing";
      });
    });
  }

  @override
  void dispose() {
    subscription.cancel();
    super.dispose();
  }

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

  int _playModeToInt(_PlayMode m) => (m == _PlayMode.follow) ? 0 : 1;

  Future<void> sendPlaybackCommand(bool play, String path) async {
    final snapshot = await ref.get();
    int count = 0;

    if (snapshot.exists) {
      final Map<dynamic, dynamic> data =
          snapshot.value as Map<dynamic, dynamic>;
      count = (data["commandsCounter"] ?? 0) as int;
    }

    await ref.set({
      "commandsCounter": count + 1,
      "fileToPlay": path,
      "playMode": _playModeToInt(_mode),
      "status": play ? "playing" : "stopped",
      "metronome": _metronomeOn,
      "speed": _chosenSpeed,
    });
  }

  // ---------- dialogs ----------
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

  // ---------- navigation ----------
  Future<void> _handleNavLeave(int index) async {
    if (isPlaying) {
      _pendingNavIndex = index;
      await _showExitDialog();
    } else {
      _navigateToTab(index);
    }
  }

  void _navigateToTab(int index) {
    if (!mounted) return;

    if (index == 0) {
      Navigator.pushReplacementNamed(context, "/home");
    } else if (index == 1) {
      Navigator.pushReplacementNamed(context, "/search");
    } else if (index == 2) {
      Navigator.pushReplacementNamed(context, "/upload");
    }
  }

  // ---------- speed dialog ----------
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

  // ---------- recompute storagePath based on current selections ----------
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
          diffs[rawDiffKey] as Map<String, dynamic>? ?? {};
      final Map<String, dynamic> handsObj =
          diffObj['hands'] as Map<String, dynamic>? ?? {};

      for (final entry in handsObj.entries) {
        final String rawHandKey = entry.key.toString();
        final String handLabel = _cleanHandsLabel(rawHandKey);
        if (_isUnknownValue(handLabel)) continue;

        final Map<String, dynamic> handObj =
            entry.value as Map<String, dynamic>? ?? {};
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

  // ---------- change settings while playing ----------
  Future<void> _attemptChangeWhilePlaying(void Function() applyChange) async {
    if (!isPlaying) {
      setState(() {
        applyChange();
        _recomputeStoragePath();
      });
      return;
    }

    final bool ok = await _showStopBeforeChangeDialog();
    if (!ok) return;

    await sendPlaybackCommand(false, _currentStoragePath);

    if (!mounted) return;

    setState(() {
      applyChange();
      _recomputeStoragePath();
    });
  }

  // ---------- memorize/follow ----------
  Future<void> _onSelectFollow() async {
    // If song is playing: show stop dialog first, stop song, set Follow + speed=1,
    // show short message, and RETURN (user can tap follow again to choose speed).
    if (isPlaying) {
      final bool stopOk = await _showStopBeforeChangeDialog();
      if (!stopOk) return;

      await sendPlaybackCommand(false, _currentStoragePath);
      if (!mounted) return;

      setState(() {
        _mode = _PlayMode.follow;
        _chosenSpeed = 1.0;
      });

      ScaffoldMessenger.of(context)
        ..clearSnackBars()
        ..showSnackBar(
          const SnackBar(
            content: Text(
              "Song stopped. Tap Follow Song again to choose speed",
            ),
            duration: Duration(seconds: 4),
          ),
        );

      return;
    }

    // Not playing: choose speed now.
    final bool ok = await _showChooseSpeedDialog();
    if (!ok) return;
    if (!mounted) return;

    setState(() => _mode = _PlayMode.follow);
  }

  Future<void> _onSelectMemorize() async {
    if (isPlaying) {
      final bool stopOk = await _showStopBeforeChangeDialog();
      if (!stopOk) return;
      await sendPlaybackCommand(false, _currentStoragePath);
      if (!mounted) return;
    }
    setState(() => _mode = _PlayMode.memorize);
  }

  // ---------- play/stop ----------
  Future<void> _onPlayStopPressed() async {
    _recomputeStoragePath();
    final String path = _currentStoragePath;

    if (path.isEmpty) return;

    if (isPlaying) {
      await sendPlaybackCommand(false, path);
    } else {
      await sendPlaybackCommand(true, path);
    }
  }

  @override
  Widget build(BuildContext context) {
    final double topPad = MediaQuery.of(context).padding.top;

    return PopScope(
      canPop: _canPop,
      onPopInvoked: (didPop) {
        if (didPop) return;

        if (isPlaying) {
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
                if (isPlaying) {
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
                snap.data!.data() as Map<String, dynamic>? ?? {};
            final Map<String, dynamic> diffs =
                data['difficulties'] as Map<String, dynamic>? ?? {};

            _lastDiffs = diffs;

            // Build map: label -> raw keys
            final Map<String, List<String>> rawKeysByDiffLabel = {};
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

            final List<String> diffLabels = rawKeysByDiffLabel.keys.toList()
              ..sort();

            // If no known labels, but we do have UNKNOWN entries -> allow UNKNOWN
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
              // No diffs at all
              _selectedDifficulty = 'UNKNOWN';
            }

            final List<String> selectedRawDiffKeys =
                rawKeysByDiffLabel[_selectedDifficulty] ?? <String>[];

            bool hasOneHand = false;
            bool hasTwoHands = false;

            _HandsChoice? initialChoiceFound;
            final String initialHandsClean = _cleanHandsLabel(
              widget.initialHands,
            );

            for (final String rawDiffKey in selectedRawDiffKeys) {
              final Map<String, dynamic> diffObj =
                  diffs[rawDiffKey] as Map<String, dynamic>? ?? {};
              final Map<String, dynamic> handsObj =
                  diffObj['hands'] as Map<String, dynamic>? ?? {};

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
                  } else if ((initialHandsClean == 'RIGHT' ||
                          initialHandsClean == 'LEFT') &&
                      (handLabel == 'RIGHT' || handLabel == 'LEFT')) {
                    initialChoiceFound = _HandsChoice.oneHand;
                  }
                }
              }
            }

            final bool showHandsSelector = hasOneHand && hasTwoHands;

            if (!_didInitHandsChoiceFromInitial) {
              if (initialChoiceFound != null) {
                _handsChoice = initialChoiceFound;
              } else {
                if (showHandsSelector) {
                  _handsChoice = (initialHandsClean == 'BOTH')
                      ? _HandsChoice.twoHands
                      : _HandsChoice.oneHand;
                } else if (hasTwoHands && !hasOneHand) {
                  _handsChoice = _HandsChoice.twoHands;
                } else if (hasOneHand && !hasTwoHands) {
                  _handsChoice = _HandsChoice.oneHand;
                }
              }
              _didInitHandsChoiceFromInitial = true;
            }

            if (!showHandsSelector) {
              if (hasTwoHands && !hasOneHand)
                _handsChoice = _HandsChoice.twoHands;
              if (hasOneHand && !hasTwoHands)
                _handsChoice = _HandsChoice.oneHand;
            }

            _recomputeStoragePath();
            final bool canPlay = _currentStoragePath.isNotEmpty;

            return LayoutBuilder(
              builder: (context, constraints) {
                final double screenHeight = MediaQuery.of(context).size.height;
                final double screenWidth = MediaQuery.of(context).size.width;

                final double coverHeight = screenHeight * 0.25;
                final double coverWidth = screenWidth * 0.6;

                return SingleChildScrollView(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 30,
                    vertical: 12,
                  ),
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
                          child: Icon(
                            Icons.music_note,
                            size: 112,
                            color: Colors.black,
                          ),
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
                            if (showDifficultySelector && showHandsSelector)
                              const SizedBox(width: 12),
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
                              style: TextStyle(
                                color: Colors.white,
                                fontSize: 16,
                              ),
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

                      const SizedBox(height: 14),

                      _SegmentedAction(
                        leftText: "Memorize Song",
                        rightText: "Follow Song",
                        selected: _mode == _PlayMode.follow ? 1 : 0,
                        onLeft: _onSelectMemorize,
                        onRight: _onSelectFollow,
                      ),

                      const SizedBox(height: 90),

                      ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          backgroundColor: isPlaying
                              ? Colors.redAccent
                              : (canPlay ? Colors.green : Colors.grey),
                          foregroundColor: Colors.black,
                          padding: const EdgeInsets.symmetric(vertical: 16),
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(10),
                          ),
                          minimumSize: const Size.fromHeight(50),
                        ),
                        onPressed: canPlay ? _onPlayStopPressed : null,
                        child: Text(
                          isPlaying ? "Stop Song" : "Learn Song",
                          style: const TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                      ),
                      const SizedBox(height: 10),
                    ],
                  ),
                );
              },
            );
          },
        ),
        bottomNavigationBar: MyBottomNavigationBar(
          currentIndex: 1,
          onTap: _handleNavLeave,
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
      children: [
        // Text(
        //   label,
        //   style: const TextStyle(color: Colors.white70, fontSize: 13),
        // ),
        // const SizedBox(height: 6),
        child,
      ],
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

class _SegmentedAction extends StatelessWidget {
  final String leftText;
  final String rightText;
  final int selected; // 0 left, 1 right
  final Future<void> Function() onLeft;
  final Future<void> Function() onRight;

  const _SegmentedAction({
    required this.leftText,
    required this.rightText,
    required this.selected,
    required this.onLeft,
    required this.onRight,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF2A2A2A),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: Colors.white12),
      ),
      child: Row(
        children: [
          Expanded(
            child: InkWell(
              borderRadius: BorderRadius.circular(14),
              onTap: () => onLeft(),
              child: _segBtn(text: leftText, isSelected: selected == 0),
            ),
          ),
          Expanded(
            child: InkWell(
              borderRadius: BorderRadius.circular(14),
              onTap: () => onRight(),
              child: _segBtn(text: rightText, isSelected: selected == 1),
            ),
          ),
        ],
      ),
    );
  }

  Widget _segBtn({required String text, required bool isSelected}) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 160),
      padding: const EdgeInsets.symmetric(vertical: 14),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(14),
        color: isSelected ? Colors.blueAccent : Colors.transparent,
      ),
      child: Center(
        child: Text(
          text,
          style: TextStyle(
            color: isSelected ? Colors.black : Colors.white,
            fontSize: 16,
            fontWeight: FontWeight.w800,
          ),
          overflow: TextOverflow.ellipsis,
        ),
      ),
    );
  }
}
