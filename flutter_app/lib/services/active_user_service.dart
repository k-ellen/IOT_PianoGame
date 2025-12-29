import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';

class ActiveUserService {
  final _firestore = FirebaseFirestore.instance;
  final _auth = FirebaseAuth.instance;

  DocumentReference get _lockRef =>
      _firestore.collection('app_state').doc('active_user');

  // ==========================
  // TRY ENTER SONG
  // ==========================
  Future<bool> tryEnterSong({required String songId}) async {
    final uid = _auth.currentUser!.uid;

    return _firestore.runTransaction((transaction) async {
      final snapshot = await transaction.get(_lockRef);

      // No document yet
      if (!snapshot.exists) {
        transaction.set(_lockRef, {
          'uid': uid,
          'songId': songId,
          'updatedAt': FieldValue.serverTimestamp(),
        });
        return true;
      }

      final data = snapshot.data() as Map<String, dynamic>?;

      // No active user
      if (data == null || data['uid'] == null) {
        transaction.set(_lockRef, {
          'uid': uid,
          'songId': songId,
          'updatedAt': FieldValue.serverTimestamp(),
        });
        return true;
      }

      // Same user re-entering
      if (data['uid'] == uid) {
        return true;
      }

      // Someone else is active
      return false;
    });
  }

  // ==========================
  // RELEASE LOCK
  // ==========================
  Future<void> release() async {
    final uid = _auth.currentUser!.uid;

    await _firestore.runTransaction((transaction) async {
      final snapshot = await transaction.get(_lockRef);

      if (!snapshot.exists) return;

      final data = snapshot.data() as Map<String, dynamic>?;

      if (data != null && data['uid'] == uid) {
        transaction.set(_lockRef, {
          'uid': null,
          'songId': null,
          'updatedAt': FieldValue.serverTimestamp(),
        });
      }
    });
  }
}
