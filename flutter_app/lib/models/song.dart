import 'package:cloud_firestore/cloud_firestore.dart';

enum SongSource { global, private }

class Song {
  final String id;
  final String name;
  final String artist;
  final String source; 
  final String genre; 

  Song({
    required this.id,
    required this.name,
    required this.artist, 
    required this.source,
    required this.genre,
  });

  factory Song.fromDoc(DocumentSnapshot doc) {   //fromDoc takes a document from Firestore and turns it into a Song object
    final data = doc.data() as Map<String, dynamic>;
    return Song(
      id: doc.id,
      name: data['name'] ?? 'Unknown',
      artist: data['artist'] ?? 'Unknown artist', 
      source: data['source'] ?? 'global',
      genre: data['genre'] ?? 'Unknown',
    );
  }
}
