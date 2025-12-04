import 'package:cloud_firestore/cloud_firestore.dart';

enum SongSource { internet, global, private }

class Song {
  final String id;
  final String name;
  final String source; 
  final String artist; 

  Song({
    required this.id,
    required this.name,
    required this.source,
    required this.artist,
  });

  factory Song.fromDoc(DocumentSnapshot doc) {   //fromDoc takes a document from Firestore and turns it into a Song object
    final data = doc.data() as Map<String, dynamic>;
    return Song(
      id: doc.id,
      name: data['name'] ?? 'Unknown',
      source: data['source'] ?? 'global',
      artist: data['artist'] ?? 'Unknown artist',
    );
  }
}
