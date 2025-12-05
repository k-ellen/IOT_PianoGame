import 'package:flutter/material.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter_app/widgets/header/my_header.dart';
import '../widgets/body/search/search_bar.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import '../models/song.dart';
import 'package:flutter_app/widgets/body/search/song_tile.dart';

class SearchScreen extends StatefulWidget {
  const SearchScreen({super.key});

  @override
  State<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends State<SearchScreen> {
  Set<String> _selectedGenres = {};
  List<String> _availableGenres = [];
  late final ScrollController _scrollController;

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
              const MySearchBar(),

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
                child: StreamBuilder<QuerySnapshot>(    //every time there is a change in Firestor the screen will automatically update
                  stream: FirebaseFirestore.instance
                      .collection('songs')
                      //.limit(200)
                      .snapshots(),
                  builder: (context, snapshot) {
                    if (snapshot.hasError) {
                      return const Center(
                        child: Text(
                          'Error loading songs',
                          style: TextStyle(color: Colors.white),
                        ),
                      );
                    }

                    if (!snapshot.hasData) {  //displays a loading circle until information arrives
                      return const Center(
                        child: CircularProgressIndicator(),
                      );
                    }

                    final allSongs = snapshot.data!.docs
                        .map((doc) => Song.fromDoc(doc))
                        .toList();

                    final genresSet = allSongs.map((s) => s.genre).toSet();
                    final genresList = genresSet.toList()..sort();

                    _availableGenres = genresList;
                    _selectedGenres = _selectedGenres.intersection(genresSet);

                    final filteredSongs = allSongs.where((song) {
                      if (_selectedGenres.isEmpty) return true;
                      return _selectedGenres.contains(song.genre);
                    }).toList();

                    if (filteredSongs.isEmpty) {
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
                      child: ListView.builder(    //displays the songs on the screen
                        controller: _scrollController,
                        itemCount: filteredSongs.length,
                        itemBuilder: (context, index) {
                          final song = filteredSongs[index];
                          return SongTile(
                            title: song.name,
                            artist: song.artist,
                            genre: song.genre,
                            index: index,
                            onTap: () {



                              //to be continued..



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

  void _openFiltersSheet() async {    //the filters window
    final currentSelection = Set<String>.from(_selectedGenres);

    final newSelection = await showModalBottomSheet<Set<String>>(   //the function waits until the user clicks "Apply" or closes
      context: context,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) {
        Set<String> tempSelection = Set.from(currentSelection);

        return StatefulBuilder(
          builder: (context, setModalState) {
            Widget buildCheckbox(String genre) {   //a function that creates one checkbox
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
                title: Text(
                  genre,
                  style: const TextStyle(color: Colors.white),
                ),
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
                        children:
                            _availableGenres.map(buildCheckbox).toList(),
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

    if (newSelection != null) {   //after closing the window updating the main screen
      setState(() {
        _selectedGenres = newSelection;
      });
    }
  }
}
