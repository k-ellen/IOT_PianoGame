import 'package:firebase_auth/firebase_auth.dart';
import 'package:cloud_firestore/cloud_firestore.dart';

class AuthService {
  final FirebaseAuth _auth = FirebaseAuth.instance;
  final FirebaseFirestore _firestore = FirebaseFirestore.instance;

  // =====================
  // SIGN UP
  // =====================
  Future<void> signUp({
    required String email,
    required String password,
    required String firstName,
    required String lastName,
  }) async {
    final userCredential = await _auth.createUserWithEmailAndPassword(
      email: email,
      password: password,
    );

    final user = userCredential.user;
    if (user == null) {
      throw FirebaseAuthException(
        code: 'user-null',
        message: 'User creation failed',
      );
    }

    final fullName = '$firstName $lastName';

    // (אופציונלי) לשמור גם ב-FirebaseAuth
    await user.updateDisplayName(fullName);

    // לשמור במסד
    await _firestore.collection('users').doc(user.uid).set({
      'uid': user.uid,
      'email': email,
      'firstName': firstName,
      'lastName': lastName,
      'fullName': fullName,
      'createdAt': FieldValue.serverTimestamp(),
    }, SetOptions(merge: true));
  }

  // =====================
  // SIGN IN
  // =====================
  Future<void> signIn({required String email, required String password}) async {
    await _auth.signInWithEmailAndPassword(email: email, password: password);
  }

  // =====================
  // SIGN OUT
  // =====================
  Future<void> signOut() async {
    await _auth.signOut();
  }
}
