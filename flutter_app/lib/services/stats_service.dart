import 'package:cloud_firestore/cloud_firestore.dart';

class StatsService {
  StatsService(this._firestore);
  final FirebaseFirestore _firestore;

  DocumentReference<Map<String, dynamic>> _generalRef(String uid) {
    return _firestore
        .collection('users')
        .doc(uid)
        .collection('stats')
        .doc('general');
  }

  Future<void> ensureGeneralStats(String uid) async {
  final ref = _generalRef(uid);
  final snap = await ref.get();

  if (!snap.exists) {
    final now = DateTime.now();
    final today = '${now.year.toString().padLeft(4, '0')}-'
        '${now.month.toString().padLeft(2, '0')}-'
        '${now.day.toString().padLeft(2, '0')}';

    await ref.set({
      'totalPracticeSeconds': 0,
      'totalPlaysCount': 0,
      'currentStreakDays': 0,
      'lastPracticeDate': today,
      'lastPlayedSongId': '',
    });
  }
}

  Future<void> onStartSong({
    required String uid,
    required String songId,
  }) async {
    final ref = _generalRef(uid);

    await ref.set({
      'totalPlaysCount': FieldValue.increment(1),
      'lastPlayedSongId': songId,
    }, SetOptions(merge: true));
  }

    Future<void> updateLastPracticeDate(String uid) async {
    final ref = _generalRef(uid);

    final now = DateTime.now();
    final today = '${now.year.toString().padLeft(4, '0')}-'
        '${now.month.toString().padLeft(2, '0')}-'
        '${now.day.toString().padLeft(2, '0')}';

    await ref.set({
      'lastPracticeDate': today,
    }, SetOptions(merge: true));
  }

}
