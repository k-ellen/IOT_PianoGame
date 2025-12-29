import 'package:firebase_auth/firebase_auth.dart';
import 'package:cloud_firestore/cloud_firestore.dart';

class AuthService {
  final FirebaseAuth _auth = FirebaseAuth.instance;
  final FirebaseFirestore _firestore = FirebaseFirestore.instance;

  // =====================
  // SIGN UP
  // =====================
  Future<void> signUp({required String email, required String password}) async {
    final userCredential = await _auth.createUserWithEmailAndPassword(
      email: email,
      password: password,
    );

    await _firestore.collection('users').doc(userCredential.user!.uid).set({
      'email': email,
      'createdAt': FieldValue.serverTimestamp(),
    });
  }

  // =====================
  // SIGN IN
  // =====================
  Future<void> signIn({required String email, required String password}) async {
    await _auth.signInWithEmailAndPassword(email: email, password: password);
  }

  // =====================
  // SIGN OUT (optional)
  // =====================
  Future<void> signOut() async {
    await _auth.signOut();
  }
}
