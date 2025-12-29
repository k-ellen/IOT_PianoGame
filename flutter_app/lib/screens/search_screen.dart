import 'package:flutter/material.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter_app/widgets/header/my_header.dart';
import '../widgets/body/search/search_bar.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import '../models/song.dart';
import 'package:flutter_app/widgets/body/search/song_tile.dart';
import 'package:flutter_app/screens/song_screen.dart';

class SearchScreen extends StatefulWidget {
  const SearchScreen({super.key});

  @override
  State<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends State<SearchScreen> {
  // ================= FILTER STATE =================
  Set<String> _selectedGenres = {};
  String? _selectedDifficulty; // single select
  String? _selectedHands; // single select

  // ================= AVAILABLE OPTIONS =================
  List<String> _availableGenres = [];
  List<String> _availableDifficulties = [];
  List<String> _availableHands = [];

  String _searchText = '';
  late final ScrollController _scrollController;

  // Order for difficulties in UI
  static const List<String> _difficultyOrder = [
    'SLOW BEGINNER',
    'BEGINNER',
    'SLOW EASY',
    'EASY',
    'MEDIUM',
    'HARD',
    'ADVANCED',
  ];

  @override
  void initState() {
    super.initState();
    _scrollController = ScrollController();
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  String _cleanGenre(dynamic raw) {
    final s = (raw ?? '').toString().trim();
    if (s.isEmpty) return 'Unknown';

    final u = s.toUpperCase().replaceAll(' ', '');
    if (u == 'UNKNOWN') return 'Unknown';
    if (u == 'POP/OTHER') return 'Pop';
    if (u == 'KIDS' || u == 'KID') return 'Children';

    final lower = s.toLowerCase();
    return lower[0].toUpperCase() + lower.substring(1);
  }

  String _cleanHands(dynamic raw) {
    final u = (raw ?? '').toString().trim().toUpperCase().replaceAll(' ', '');

    if (u == 'L+R' || u == 'LR' || u == 'BOTH' || u == 'L&R') return 'L+R';
    if (u == 'L') return 'L';
    if (u == 'R') return 'R';

    return 'UNKNOWN';
  }

  String _cleanDifficulty(dynamic raw) {
    final u = (raw ?? '').toString().trim().toUpperCase();

    if (u.contains('SLOW') && u.contains('BEGINNER')) return 'SLOW BEGINNER';
    if (u.contains('SLOW') && u.contains('EASY')) return 'SLOW EASY';

    // Regular levels
    if (u.contains('BEGINNER')) return 'BEGINNER';
    if (u.contains('EASY')) return 'EASY';
    if (u.contains('INTERMEDIATE')) return 'INTERMEDIATE';
    if (u.contains('MEDIUM')) return 'MEDIUM'; // unify
    if (u.contains('HARD')) return 'HARD';
    if (u.contains('ADVANCED')) return 'ADVANCED';

    return 'UNKNOWN';
  }

  bool _isUnknown(String v) => v.trim().toUpperCase() == 'UNKNOWN';

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const MyHeader(title: 'Search'),
              const SizedBox(height: 20),

              MySearchBar(
                onChanged: (v) => setState(() => _searchText = v.toLowerCase()),
              ),

              const SizedBox(height: 16),

              Row(
                children: [
                  _filterButton(
                    label: _selectedGenres.isEmpty
                        ? 'Genre'
                        : 'Genre (${_selectedGenres.length})',
                    icon: Icons.category,
                    onTap: _openGenreSheet,
                  ),
                  _filterButton(
                    label: _selectedDifficulty ?? 'Difficulties',
                    icon: Icons.speed,
                    onTap: _openDifficultySheet,
                    useEllipsis: _selectedDifficulty != null,
                  ),
                  _filterButton(
                    label: _selectedHands ?? 'Hands',
                    icon: Icons.pan_tool,
                    onTap: _openHandsSheet,
                  ),
                ],
              ),

              const SizedBox(height: 16),

              Expanded(
                child: StreamBuilder<QuerySnapshot>(
                  stream: FirebaseFirestore.instance
                      .collection('songsNEW_midi')
                      .snapshots(),
                  builder: (context, snap2) {
                    if (snap2.hasError) {
                      return const Center(
                        child: Text(
                          'Error loading songs',
                          style: TextStyle(color: Colors.white),
                        ),
                      );
                    }

                    if (!snap2.hasData) {
                      return const Center(child: CircularProgressIndicator());
                    }

                    final variants = <SongVariant>[];

                    // ========== ONLY collection: songsNEW_midi ==========
                    for (final doc in snap2.data!.docs) {
                      final d = doc.data() as Map<String, dynamic>;

                      final diffs = d['difficulties'];
                      if (diffs is! Map<String, dynamic>) continue;

                      diffs.forEach((diffKey, diffVal) {
                        if (diffVal is! Map<String, dynamic>) return;

                        final handsObj = diffVal['hands'];
                        if (handsObj is! Map<String, dynamic>) return;

                        final diff = _cleanDifficulty(diffKey);

                        handsObj.forEach((handKey, handVal) {
                          if (handVal is! Map<String, dynamic>) return;

                          final sp = handVal['storagePath'];
                          if (sp is! String || sp.isEmpty) return;

                          final hand = _cleanHands(handKey);

                          variants.add(
                            SongVariant(
                              title: (d['name'] ?? '') as String,
                              artist: (d['artist'] ?? '') as String,
                              genre: _cleanGenre(d['genre']),
                              difficulty: diff,
                              hands: hand,
                              storagePath: sp,
                              sourceCollection: 'songsNEW_midi',
                              songId: doc.id,
                            ),
                          );
                        });
                      });
                    }

                    _availableGenres =
                        variants
                            .map((v) => v.genre)
                            .where((g) => !_isUnknown(g) && g.trim().isNotEmpty)
                            .toSet()
                            .toList()
                          ..sort();

                    _availableDifficulties =
                        variants
                            .map((v) => v.difficulty)
                            .where((d) => !_isUnknown(d))
                            .toSet()
                            .toList()
                          ..sort(
                            (a, b) => _difficultyOrder
                                .indexOf(a)
                                .compareTo(_difficultyOrder.indexOf(b)),
                          );

                    _availableHands =
                        variants
                            .map((v) => v.hands)
                            .where((h) => !_isUnknown(h))
                            .toSet()
                            .toList()
                          ..sort();

                    _selectedGenres = _selectedGenres.intersection(
                      _availableGenres.toSet(),
                    );

                    if (_selectedDifficulty != null &&
                        !_availableDifficulties.contains(_selectedDifficulty)) {
                      _selectedDifficulty = null;
                    }

                    if (_selectedHands != null &&
                        !_availableHands.contains(_selectedHands)) {
                      _selectedHands = null;
                    }

                    final filtered = variants.where((v) {
                      if (_selectedGenres.isNotEmpty &&
                          !_selectedGenres.contains(v.genre)) {
                        return false;
                      }

                      if (_selectedDifficulty != null &&
                          v.difficulty != _selectedDifficulty) {
                        return false;
                      }

                      if (_selectedHands != null && v.hands != _selectedHands) {
                        return false;
                      }

                      if (_searchText.isNotEmpty) {
                        final q = _searchText;
                        final inName = v.title.toLowerCase().contains(q);
                        final inArtist = v.artist.toLowerCase().contains(q);
                        if (!inName && !inArtist) return false;
                      }

                      return true;
                    }).toList();

                    if (filtered.isEmpty) {
                      return const Center(
                        child: Text(
                          'No songs to show',
                          style: TextStyle(color: Colors.white70),
                        ),
                      );
                    }

                    return Scrollbar(
                      controller: _scrollController,
                      thumbVisibility: true,
                      child: ListView.builder(
                        controller: _scrollController,
                        itemCount: filtered.length,
                        itemBuilder: (context, i) {
                          final v = filtered[i];

                          return SongTile(
                            title: v.title,
                            artist: v.artist,
                            genre: v.genre,
                            difficulties: v.difficulty,
                            hands: v.hands,
                            index: i,
                            onTap: () {
                              Navigator.push(
                                context,
                                MaterialPageRoute(
                                  builder: (_) => SongScreen(
                                    songId: v.songId,
                                    title: v.title,
                                    artist: v.artist,
                                    initialDifficulty: v.difficulty,
                                    initialHands: v.hands,
                                  ),
                                ),
                              );
                            },
                          );
                        },
                      ),
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 1),
    );
  }

  // ================= Buttons =================

  Widget _filterButton({
    required String label,
    required IconData icon,
    required VoidCallback onTap,
    bool useEllipsis = false,
  }) {
    return Expanded(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4),
        child: ElevatedButton(
          onPressed: onTap,
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.grey[800],
            foregroundColor: Colors.white,
            alignment: Alignment.centerLeft,
            padding: const EdgeInsets.symmetric(horizontal: 12),
          ),
          child: SizedBox(
            width: double.infinity,
            child: Row(
              mainAxisAlignment: MainAxisAlignment.start,
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                Icon(icon, size: 18),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    label,
                    maxLines: 1,
                    softWrap: false,
                    overflow: useEllipsis
                        ? TextOverflow.ellipsis
                        : TextOverflow.visible,
                    style: const TextStyle(fontSize: 13),
                    textAlign: TextAlign.start,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  // ================= Sheets =================

  void _openGenreSheet() async {
    final temp = Set<String>.from(_selectedGenres);

    final res = await showModalBottomSheet<Set<String>>(
      context: context,
      isScrollControlled: true,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) {
        return StatefulBuilder(
          builder: (context, setM) {
            return SafeArea(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Text(
                      'Filter by genre',
                      style: TextStyle(
                        color: Colors.white,
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 12),
                    SizedBox(
                      height: MediaQuery.of(context).size.height * 0.45,
                      child: ListView(
                        children: _availableGenres.map((g) {
                          return CheckboxListTile(
                            title: Text(
                              g,
                              style: const TextStyle(color: Colors.white),
                            ),
                            value: temp.contains(g),
                            onChanged: (v) => setM(() {
                              if (v == true) {
                                temp.add(g);
                              } else {
                                temp.remove(g);
                              }
                            }),
                            activeColor: const Color.fromARGB(255, 5, 229, 5),
                            checkColor: Colors.black,
                          );
                        }).toList(),
                      ),
                    ),
                    const SizedBox(height: 12),
                    ElevatedButton(
                      onPressed: () => Navigator.pop(context, temp),
                      child: const Text('Apply'),
                    ),
                  ],
                ),
              ),
            );
          },
        );
      },
    );

    if (res != null) {
      setState(() => _selectedGenres = res);
    }
  }

  void _openDifficultySheet() async {
    final res = await _openRadioSheet(
      title: 'Filter by difficulties',
      options: _availableDifficulties,
      current: _selectedDifficulty,
    );
    setState(() => _selectedDifficulty = res);
  }

  void _openHandsSheet() async {
    final res = await _openRadioSheet(
      title: 'Filter by hands',
      options: _availableHands,
      current: _selectedHands,
    );
    setState(() => _selectedHands = res);
  }

  Future<String?> _openRadioSheet({
    required String title,
    required List<String> options,
    required String? current,
  }) async {
    return showModalBottomSheet<String?>(
      context: context,
      isScrollControlled: true,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) {
        String? temp = current;

        return StatefulBuilder(
          builder: (context, setM) {
            return SafeArea(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 12),
                    SizedBox(
                      height: MediaQuery.of(context).size.height * 0.45,
                      child: ListView(
                        children: [
                          RadioGroup<String>(
                            groupValue: temp,
                            onChanged: (v) => setM(() => temp = v),
                            child: Column(
                              children: [
                                for (final o in options)
                                  RadioListTile<String>(
                                    value: o,
                                    title: Text(
                                      o,
                                      style: const TextStyle(
                                        color: Colors.white,
                                      ),
                                    ),
                                  ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        Expanded(
                          child: OutlinedButton(
                            onPressed: () => Navigator.pop(context, null),
                            child: const Text('Clear'),
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: ElevatedButton(
                            onPressed: () => Navigator.pop(context, temp),
                            child: const Text('Apply'),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            );
          },
        );
      },
    );
  }
}
