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
  String? _selectedHands;      // single select

  // ================= AVAILABLE OPTIONS =================
  List<String> _availableGenres = [];
  List<String> _availableDifficulties = [];
  List<String> _availableHands = [];

  String _searchText = '';
  late final ScrollController _scrollController;

  // Order for difficulties in UI
  static const List<String> _difficultyOrder = [
    'BEGINNER',
    'EASY',
    'SLOW EASY',
    'SLOW BEGINNER',
    'INTERMEDIATE',
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
    if (u.contains('MEDIUM')) return 'INTERMEDIATE'; // unify
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
                  stream: FirebaseFirestore.instance.collection('songs').snapshots(),
                  builder: (context, snap1) {
                    if (snap1.hasError) {
                      return const Center(
                        child: Text('Error loading songs',
                            style: TextStyle(color: Colors.white)),
                      );
                    }
                    if (!snap1.hasData) {
                      return const Center(child: CircularProgressIndicator());
                    }

                    return StreamBuilder<QuerySnapshot>(
                      stream: FirebaseFirestore.instance.collection('SONGS').snapshots(),
                      builder: (context, snap2) {
                        if (snap2.hasError) {
                          return const Center(
                            child: Text('Error loading SONGS',
                                style: TextStyle(color: Colors.white)),
                          );
                        }
                        if (!snap2.hasData) {
                          return const Center(child: CircularProgressIndicator());
                        }

                        final variants = <SongVariant>[];

                        // ========== OLD collection: songs ==========
                        for (final doc in snap1.data!.docs) {
                          final d = doc.data() as Map<String, dynamic>;
                          final path = d['storagePath'];
                          if (path is! String || path.isEmpty) continue;

                          variants.add(SongVariant(
                            title: (d['name'] ?? '') as String,
                            artist: (d['artist'] ?? '') as String,
                            genre: _cleanGenre(d['genre']),
                            difficulty: _cleanDifficulty(d['difficulties']),
                            hands: _cleanHands(d['hands']),
                            storagePath: path,
                            sourceCollection: 'songs',
                          ));
                        }

                        // ========== NEW collection: SONGS ==========
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

                              variants.add(SongVariant(
                                title: (d['name'] ?? '') as String,
                                artist: (d['artist'] ?? '') as String,
                                genre: _cleanGenre(d['genre']),
                                difficulty: diff,
                                hands: hand,
                                storagePath: sp,
                                sourceCollection: 'SONGS',
                              ));
                            });
                          });
                        }

                        // ========== Build FILTER OPTIONS from the SHOWN values ==========
                        _availableGenres = variants
                            .map((v) => v.genre)
                            .where((g) => !_isUnknown(g) && g.trim().isNotEmpty)
                            .toSet()
                            .toList()
                          ..sort();

                        _availableDifficulties = variants
                            .map((v) => v.difficulty)
                            .where((d) => !_isUnknown(d))
                            .toSet()
                            .toList()
                          ..sort((a, b) =>
                              _difficultyOrder.indexOf(a).compareTo(_difficultyOrder.indexOf(b)));

                        _availableHands = variants
                            .map((v) => v.hands)
                            .where((h) => !_isUnknown(h))
                            .toSet()
                            .toList()
                          ..sort();

                        // keep selections valid
                        _selectedGenres =
                            _selectedGenres.intersection(_availableGenres.toSet());

                        if (_selectedDifficulty != null &&
                            !_availableDifficulties.contains(_selectedDifficulty)) {
                          _selectedDifficulty = null;
                        }

                        if (_selectedHands != null &&
                            !_availableHands.contains(_selectedHands)) {
                          _selectedHands = null;
                        }

                        // ========== Apply filters (exactly by shown values) ==========
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
                            child: Text('No songs to show',
                                style: TextStyle(color: Colors.white70)),
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
                                        storagePath: v.storagePath,
                                        title: v.title,
                                        artist: v.artist,
                                      ),
                                    ),
                                  );
                                },
                              );
                            },
                          ),
                        );
                      },
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
  }) {
    return Expanded(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4),
        child: ElevatedButton.icon(
          onPressed: onTap,
          icon: Icon(icon, size: 18),
          label: Text(label, overflow: TextOverflow.ellipsis),
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.grey[800],
            foregroundColor: Colors.white,
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
                            title: Text(g, style: const TextStyle(color: Colors.white)),
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
                                    title: Text(o, style: const TextStyle(color: Colors.white)),
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


/*
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
  Set<String> _selectedGenres = {};
  List<String> _availableGenres = [];
  late final ScrollController _scrollController;
    String _searchText = '';

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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              MyHeader(title: 'Search'),
              const SizedBox(height: 24),
              MySearchBar(
              onChanged: (value) {
                setState(() {
                  _searchText = value.toLowerCase(); 
                });
              },
            ),

              const SizedBox(height: 16),

              Align(
                alignment: Alignment.centerLeft,
                child: ElevatedButton.icon(
                  onPressed: _openFiltersSheet,
                  icon: const Icon(Icons.filter_list),
                  label: const Text('Filter songs'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.grey[800],
                    foregroundColor: Colors.white,
                  ),
                ),
              ),

              const SizedBox(height: 16),

              Expanded(
                child: StreamBuilder<QuerySnapshot>(
                  stream: FirebaseFirestore.instance.collection('songs').snapshots(),
                  builder: (context, snap1) {
                    if (snap1.hasError) {
                      return const Center(
                        child: Text('Error loading songs', style: TextStyle(color: Colors.white)),
                      );
                    }
                    if (!snap1.hasData) {
                      return const Center(child: CircularProgressIndicator());
                    }

                    return StreamBuilder<QuerySnapshot>(
                      stream: FirebaseFirestore.instance.collection('SONGS').snapshots(),
                      builder: (context, snap2) {
                        if (snap2.hasError) {
                          return const Center(
                            child: Text('Error loading SONGS', style: TextStyle(color: Colors.white)),
                          );
                        }
                        if (!snap2.hasData) {
                          return const Center(child: CircularProgressIndicator());
                        }

                        final variants = <SongVariant>[];

                        for (final doc in snap1.data!.docs) {
                          final data = doc.data() as Map<String, dynamic>;
                          final path = (data['storagePath'] ?? '') as String;
                          if (path.isEmpty) continue;

                          variants.add(SongVariant(
                            title: (data['name'] ?? '') as String,
                            artist: (data['artist'] ?? '') as String,
                            genre: (data['genre'] ?? '') as String,
                            difficulty: (data['difficulties'] ?? 'UNKNOWN') as String,
                            hands: (data['hands'] ?? 'UNKNOWN') as String,
                            storagePath: path,
                            sourceCollection: 'songs',
                          ));
                        }

                        for (final doc in snap2.data!.docs) {
                          final data = doc.data() as Map<String, dynamic>;
                          final title = (data['name'] ?? '') as String;
                          final artist = (data['artist'] ?? '') as String;
                          final genre = (data['genre'] ?? '') as String;

                          final diffs = data['difficulties'];
                          if (diffs is! Map<String, dynamic>) continue;

                          diffs.forEach((diffKey, diffVal) {
                            if (diffVal is! Map<String, dynamic>) return;

                            final handsObj = diffVal['hands'];
                            if (handsObj is! Map<String, dynamic>) return;

                            handsObj.forEach((handKey, handVal) {
                              if (handVal is! Map<String, dynamic>) return;

                              final sp = handVal['storagePath'];
                              if (sp is! String || sp.isEmpty) return;

                              variants.add(SongVariant(
                                title: title,
                                artist: artist,
                                genre: genre,
                                difficulty: diffKey,
                                hands: handKey,
                                storagePath: sp,
                                sourceCollection: 'SONGS',
                              ));
                            });
                          });
                        }

                        final allGenresSet = variants.map((v) => v.genre).toSet();
                        final genresList = allGenresSet.toList();
                        genresList.sort((a, b) {
                          if (a == "User Upload") return -1;
                          if (b == "User Upload") return 1;
                          return a.compareTo(b);
                        });
                        _availableGenres = genresList;
                        _selectedGenres = _selectedGenres.intersection(allGenresSet);

                        final filtered = variants.where((v) {
                          if (_selectedGenres.isNotEmpty && !_selectedGenres.contains(v.genre)) {
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
                            child: Text('No songs to show', style: TextStyle(color: Colors.white70)),
                          );
                        }

                        return Scrollbar(
                          controller: _scrollController,
                          thumbVisibility: true,
                          child: ListView.builder(
                            controller: _scrollController,
                            itemCount: filtered.length,
                            itemBuilder: (context, index) {
                              final v = filtered[index];

                              return SongTile(
                                title: v.title,
                                artist: v.artist,
                                genre: v.genre,
                                difficulties: v.difficulty, 
                                hands: v.hands,             
                                index: index,
                                onTap: () {
                                  Navigator.push(
                                    context,
                                    MaterialPageRoute(
                                      builder: (context) => SongScreen(
                                        storagePath: v.storagePath,
                                        title: v.title,
                                        artist: v.artist,
                                      ),
                                    ),
                                  );
                                },
                              );
                            },
                          ),
                        );
                      },
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

  void _openFiltersSheet() async {
    //the filters window
    final currentSelection = Set<String>.from(_selectedGenres);

    final newSelection = await showModalBottomSheet<Set<String>>(
      //the function waits until the user clicks "Apply" or closes
      context: context,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) {
        Set<String> tempSelection = Set.from(currentSelection);

        return StatefulBuilder(
          builder: (context, setModalState) {
            Widget buildCheckbox(String genre) {
              //a function that creates one checkbox
              return CheckboxListTile(
                value: tempSelection.contains(genre),
                onChanged: (value) {
                  setModalState(() {
                    if (value == true) {
                      tempSelection.add(genre);
                    } else {
                      tempSelection.remove(genre);
                    }
                  });
                },
                title: Text(genre, style: const TextStyle(color: Colors.white)),
                activeColor: const Color.fromARGB(255, 5, 229, 5),
                checkColor: Colors.black,
              );
            }

            return SafeArea(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
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
                      height: 300,
                      child: ListView(
                        children: _availableGenres.map(buildCheckbox).toList(),
                      ),
                    ),

                    const SizedBox(height: 12),
                    ElevatedButton(
                      onPressed: () {
                        Navigator.pop(context, tempSelection);
                      },
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

    if (newSelection != null) {
      //after closing the window updating the main screen
      setState(() {
        _selectedGenres = newSelection;
      });
    }
  }
}
*/