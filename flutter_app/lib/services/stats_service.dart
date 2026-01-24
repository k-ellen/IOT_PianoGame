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
  'hardPlaysCount': 0,
  'currentStreakDays': 0,
  'maxStreakDays': 0,
  'lastPracticeDay': null,
  'lastPracticeDate': today,
  'lastPlayedSongId': '',
});
  }
}

 Future<void> onStartSong({
  required String uid,
  required String songId,
  required String difficultyLabel,
}) async {
  final ref = _generalRef(uid);

  final d = difficultyLabel.trim().toUpperCase();

  final Map<String, dynamic> update = {
    'totalPlaysCount': FieldValue.increment(1),
    'lastPlayedSongId': songId,
  };

  if (d.contains('HARD') || d.contains('ADVANCED')) {
    update['hardPlaysCount'] = FieldValue.increment(1);
  }

  await ref.set(update, SetOptions(merge: true));
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

  Future<void> registerPracticeDay(String uid) async {
  final ref = _generalRef(uid);

  await _firestore.runTransaction((tx) async {
    final snap = await tx.get(ref);
    final data = snap.data() ?? <String, dynamic>{};

    int toInt(dynamic v) {
      if (v == null) return 0;
      if (v is int) return v;
      if (v is num) return v.toInt();
      return 0;
    }

    final int currentStreak = toInt(data['currentStreakDays']);
    final int maxStreak = toInt(data['maxStreakDays']);

    final Timestamp? lastTs = data['lastPracticeDay'] as Timestamp?;

    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);

    DateTime? lastDay;
    if (lastTs != null) {
      final d = lastTs.toDate();
      lastDay = DateTime(d.year, d.month, d.day);
    }

    int newCurrent;
    if (lastDay == null) {
      newCurrent = 1;
    } else {
      final diffDays = today.difference(lastDay).inDays;

      if (diffDays == 0) {
        newCurrent = currentStreak; // כבר נרשם היום
      } else if (diffDays == 1) {
        newCurrent = currentStreak + 1; // רצף
      } else {
        newCurrent = 1; // נשבר
      }
    }

    final int newMax = newCurrent > maxStreak ? newCurrent : maxStreak;

    final String todayStr = '${today.year.toString().padLeft(4, '0')}-'
        '${today.month.toString().padLeft(2, '0')}-'
        '${today.day.toString().padLeft(2, '0')}';

    tx.set(ref, {
      'currentStreakDays': newCurrent,
      'maxStreakDays': newMax,
      'lastPracticeDay': Timestamp.fromDate(today),
      'lastPracticeDate': todayStr, 
    }, SetOptions(merge: true));
  });
}


}