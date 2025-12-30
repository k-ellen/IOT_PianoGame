import 'dart:async';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

import '../widgets/footer/bottom_navigation_bar.dart';
import '../widgets/header/my_header.dart';
import '../widgets/my_button.dart';

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
  late String _selectedHands;

  String _currentStoragePath = '';

  @override
  void initState() {
    super.initState();

    _selectedDifficulty = widget.initialDifficulty;
    _selectedHands = widget.initialHands;

    ref = FirebaseDatabase.instance.ref("esp32API/playCommand");

    subscription = ref.onValue.listen((event) {
      final Map<dynamic, dynamic>? data =
          event.snapshot.value as Map<dynamic, dynamic>?;

      if (!mounted) return;
      if (data == null) return;

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
      "playMode": 0,
      "status": play ? "playing" : "stopped",
    });
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

        // ✅ FIX: AppBar שמתחשב ב-status bar (כמו במסך Search)
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

            final bool selectedIsUnknown =
                _isUnknownValue(_cleanDifficulty(_selectedDifficulty));

            final Map<String, String> nonUnknownDiffs = {};
            String? unknownRawKey;

            for (final entry in diffs.entries) {
              final String rawKey = entry.key.toString();
              final String label = _cleanDifficulty(rawKey);

              if (_isUnknownValue(label)) {
                unknownRawKey ??= rawKey;
              } else {
                nonUnknownDiffs[label] = rawKey;
              }
            }

            final List<String> availableDiffs = nonUnknownDiffs.keys.toList()
              ..sort();

            if (selectedIsUnknown &&
                nonUnknownDiffs.isNotEmpty &&
                unknownRawKey != null) {
              availableDiffs.add('UNKNOWN');
            }

            final Map<String, String> diffKeyByLabel = {
              ...nonUnknownDiffs,
              if (availableDiffs.contains('UNKNOWN') && unknownRawKey != null)
                'UNKNOWN': unknownRawKey!,
            };

            if (availableDiffs.isNotEmpty &&
                !availableDiffs.contains(_selectedDifficulty)) {
              _selectedDifficulty = availableDiffs.first;
            }

            String selectedRawDiffKey = diffKeyByLabel[_selectedDifficulty] ?? '';
            if (selectedRawDiffKey.isEmpty && diffs.isNotEmpty) {
              selectedRawDiffKey = diffs.keys.first.toString();
            }

            final Map<String, dynamic> diffObj = selectedRawDiffKey.isEmpty
                ? <String, dynamic>{}
                : (diffs[selectedRawDiffKey] as Map<String, dynamic>? ?? {});

            final Map<String, dynamic> handsObj =
                diffObj['hands'] as Map<String, dynamic>? ?? {};

            final List<String> availableHands =
                handsObj.keys.cast<String>().toList()
                  ..sort();

            if (availableHands.isNotEmpty &&
                !availableHands.contains(_selectedHands)) {
              _selectedHands = availableHands.first;
            }

            final Map<String, dynamic> selectedHandObj =
                handsObj[_selectedHands] as Map<String, dynamic>? ?? {};

            final String currentStoragePath =
                (selectedHandObj['storagePath'] as String?) ?? '';

            _currentStoragePath = currentStoragePath;

            return SafeArea(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  children: [
                    Container(
                      width: double.infinity,
                      height: MediaQuery.of(context).size.height * 0.45,
                      decoration: BoxDecoration(
                        color: const Color(0xFFFFD54F),
                        borderRadius: BorderRadius.circular(6),
                      ),
                      child: const Icon(
                        Icons.music_note,
                        size: 128,
                        color: Colors.black,
                      ),
                    ),
                    const SizedBox(height: 40),
                    Text(
                      widget.title,
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 24,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Text(
                      widget.artist,
                      style: const TextStyle(
                        color: Colors.white70,
                        fontSize: 18,
                      ),
                    ),
                    const SizedBox(height: 40),

                    if (availableDiffs.length > 1)
                      Row(
                        children: [
                          const Text(
                            'Difficulty:',
                            style: TextStyle(color: Colors.white),
                          ),
                          const SizedBox(width: 12),
                          DropdownButton<String>(
                            value: _selectedDifficulty,
                            dropdownColor: const Color(0xFF2A2A2A),
                            items: availableDiffs
                                .map(
                                  (d) => DropdownMenuItem<String>(
                                    value: d,
                                    child: Text(
                                      d,
                                      style: const TextStyle(color: Colors.white),
                                    ),
                                  ),
                                )
                                .toList(),
                            onChanged: (v) {
                              if (v == null) return;
                              setState(() => _selectedDifficulty = v);
                            },
                          ),
                        ],
                      ),

                    const SizedBox(height: 12),

                    if (availableHands.length > 1)
                      Row(
                        children: [
                          const Text(
                            'Hands:',
                            style: TextStyle(color: Colors.white),
                          ),
                          const SizedBox(width: 12),
                          DropdownButton<String>(
                            value: _selectedHands,
                            dropdownColor: const Color(0xFF2A2A2A),
                            items: availableHands
                                .map(
                                  (h) => DropdownMenuItem<String>(
                                    value: h,
                                    child: Text(
                                      h,
                                      style: const TextStyle(color: Colors.white),
                                    ),
                                  ),
                                )
                                .toList(),
                            onChanged: (v) {
                              if (v == null) return;
                              setState(() => _selectedHands = v);
                            },
                          ),
                        ],
                      ),

                    const SizedBox(height: 30),

                    MyButton(
                      title: isPlaying ? "Stop Song" : "Learn Song",
                      color: isPlaying ? Colors.redAccent : Colors.blueAccent,
                      onPressed: currentStoragePath.isEmpty
                          ? null
                          : () => sendPlaybackCommand(!isPlaying, currentStoragePath),
                    ),
                  ],
                ),
              ),
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
