import 'dart:async';
import 'package:firebase_database/firebase_database.dart' as rtdb;
import 'package:flutter/material.dart';

typedef IsOwnerFn = bool Function();
typedef IsPlayingFn = bool Function();

class PlayFailureListener {
  PlayFailureListener({
    required rtdb.DatabaseReference playRef,
    required IsOwnerFn isOwner,
    required IsPlayingFn isPlaying,
  })  : _playRef = playRef,
        _isOwner = isOwner,
        _isPlaying = isPlaying;

  final rtdb.DatabaseReference _playRef;
  final IsOwnerFn _isOwner;
  final IsPlayingFn _isPlaying;

  StreamSubscription<rtdb.DatabaseEvent>? _sub;

  int? _baseline;
  bool _showing = false;
  String _lastShown = '';

  void start(BuildContext context) {
    _sub = _playRef.onValue.listen((event) async {
      final Map<dynamic, dynamic>? data =
          event.snapshot.value as Map<dynamic, dynamic>?;
      if (data == null) return;

      final String status = (data['status'] ?? 'stopped').toString();
      final bool playing = status == 'playing';

      final int fc = (data['failureCounter'] is int)
          ? (data['failureCounter'] as int)
          : int.tryParse((data['failureCounter'] ?? '0').toString()) ?? 0;

      final String lastFailure = (data['lastFailure'] ?? '').toString();

      final bool activeMine = _isOwner() && playing && _isPlaying();
      if (!activeMine) return;

      _baseline ??= fc;

      if (fc == _baseline) return; // <-- changed

      _baseline = fc;

      final String msg =
          lastFailure.trim().isEmpty ? 'Unknown error' : lastFailure.trim();

      if (_showing) return;
      if (_lastShown == msg) return;
      _lastShown = msg;

      if (!context.mounted) return;

      _showing = true;
      try {
        await showDialog<void>(
          context: context,
          builder: (_) => AlertDialog(
            backgroundColor: const Color.fromARGB(255, 23, 23, 23),
            title: const Text(
              'Playback error',
              style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
            ),
            content: Text(
              msg,
              style: const TextStyle(color: Colors.white70),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text('OK', style: TextStyle(color: Colors.white)),
              ),
            ],
          ),
        );
      } finally {
        _showing = false;
      }
    });
  }

  Future<void> stop() async {
    await _sub?.cancel();
    _sub = null;
    _baseline = null;
    _showing = false;
    _lastShown = '';
  }
}
