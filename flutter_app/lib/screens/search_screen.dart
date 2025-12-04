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
  Set<SongSource> _selectedSources = {
    SongSource.internet,
    SongSource.global,
    SongSource.private,
  };

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

                    final filteredSongs = allSongs.where((song) {
                      if (song.source == 'internet' &&
                          _selectedSources.contains(SongSource.internet)) {
                        return true;
                      }
                      if (song.source == 'global' &&
                          _selectedSources.contains(SongSource.global)) {
                        return true;
                      }
                      if (song.source == 'private' &&
                          _selectedSources.contains(SongSource.private)) {
                        return true;
                      }
                      return false;
                    }).toList();

                    if (filteredSongs.isEmpty) {
                      return const Center(
                        child: Text(
                          'No songs to show',
                          style: TextStyle(color: Colors.white70),
                        ),
                      );
                    }

                    return ListView.builder(    //displays the songs on the screen
                      itemCount: filteredSongs.length,
                      itemBuilder: (context, index) {
                        final song = filteredSongs[index];
                        return SongTile(
                          title: song.name,
                          artist: song.artist,
                          source: song.source,
                          index: index,
                          onTap: () {



                            //to be continued..



                          },
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

  void _openFiltersSheet() async {    //the filters window
    final newSelection = await showModalBottomSheet<Set<SongSource>>(   //the function waits until the user clicks "Apply" or closes
      context: context,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) {
        Set<SongSource> tempSelection = Set.from(_selectedSources);

        return StatefulBuilder(
          builder: (context, setModalState) {
            Widget buildCheckbox(SongSource source, String label) {   //a function that creates one checkbox
              return CheckboxListTile(
                value: tempSelection.contains(source),
                onChanged: (value) {
                  setModalState(() {
                    if (value == true) {
                      tempSelection.add(source);
                    } else {
                      tempSelection.remove(source);
                    }
                  });
                },
                title: Text(
                  label,
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
                      'Filter by source',
                      style: TextStyle(
                        color: Colors.white,
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 12),
                    buildCheckbox(SongSource.internet, 'Internet'),
                    buildCheckbox(SongSource.global, 'Global'),
                    buildCheckbox(SongSource.private, 'Private'),
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
        _selectedSources = newSelection;
      });
    }
  }
}
