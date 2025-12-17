import 'package:cloud_firestore/cloud_firestore.dart';

enum SongSource { global, private }


class Song {
  final String id;
  final String name;
  final String artist;
  final String source;
  final String genre;
  final String storagePath;

  final String difficulties;
  final String hands;

  Song({
    required this.id,
    required this.name,
    required this.artist,
    required this.source,
    required this.genre,
    required this.storagePath,
    required this.difficulties,
    required this.hands,
  });

  factory Song.fromDoc(DocumentSnapshot doc) {
    final data = doc.data() as Map<String, dynamic>;
    return Song(
      id: doc.id,
      name: (data['name'] ?? 'Unknown') as String,
      artist: (data['artist'] ?? 'Unknown artist') as String,
      source: (data['source'] ?? 'global') as String,
      genre: (data['genre'] ?? 'Unknown') as String,
      storagePath: (data['storagePath'] ?? '') as String,
      difficulties: (data['difficulties'] ?? 'UNKNOWN') as String,
      hands: (data['hands'] ?? 'UNKNOWN') as String,
    );
  }
}

class SongVariant {
  final String title;
  final String artist;
  final String genre;

  final String difficulty; 
  final String hands;      
  final String storagePath;

  final String sourceCollection; 
  final String songId;

  SongVariant({
    required this.title,
    required this.artist,
    required this.genre,
    required this.difficulty,
    required this.hands,
    required this.storagePath,
    required this.sourceCollection,
    required this.songId,
  });
}