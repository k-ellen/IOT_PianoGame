import 'dart:io';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:dotted_border/dotted_border.dart';
import 'package:file_picker/file_picker.dart';
import 'package:firebase_storage/firebase_storage.dart';
import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import '../widgets/header/my_header.dart';
import '../widgets/my_button.dart';

class UploadScreen extends StatefulWidget {
  const UploadScreen({super.key});

  @override
  State<UploadScreen> createState() => _UploadScreenState();
}

class _UploadScreenState extends State<UploadScreen> {
  File? _selectedFile;
  bool _isUploading = false;

  // Choose ONE collection name across the app.
  // Your SongScreen reads from: songsNEW_midi
  static const String _songsCollection = 'songsNEW_midi';

  Future<void> _pickFile() async {
    try {
      final FilePickerResult? result = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['mid', 'midi'],
        withData: false,
      );

      if (result == null || result.files.isEmpty) {
        return; // user cancelled
      }

      final String? path = result.files.single.path;
      if (path == null) {
        _showSnack("Couldn't read file path");
        return;
      }

      final File file = File(path);
      final String ext = file.path.split('.').last.toLowerCase();
      if (ext != 'mid' && ext != 'midi') {
        _showSnack("Please select a .mid/.midi MIDI file");
        return;
      }

      setState(() {
        _selectedFile = file;
      });

      await _uploadFile();
    } catch (e) {
      _showSnack("Error picking file");
    }
  }

  Future<void> _uploadFile() async {
    if (_selectedFile == null) {
      _showSnack("Please select a MIDI file first");
      return;
    }

    try {
      setState(() => _isUploading = true);

      // 1) Upload to Firebase Storage
      final String fileName = _selectedFile!.path.split('/').last;
      final String baseName = fileName
          .replaceAll('.mid', '')
          .replaceAll('.midi', '');

      // Use a timestamped path to avoid collisions
      final String storagePath =
          "uploadedSongs/${DateTime.now().millisecondsSinceEpoch}_$fileName";

      final Reference storageRef =
          FirebaseStorage.instance.ref().child(storagePath);

      await storageRef.putFile(_selectedFile!);

      // 2) Add Firestore document to the SAME collection your app uses
      //    This structure matches what SongScreen expects:
      //    difficulties -> hands -> storagePath
      await _addSongToFirestore(
        title: baseName,
        artist: "Unknown",
        storagePath: storagePath,
      );

      _showSnack("File uploaded successfully!");
      setState(() {
        _selectedFile = null;
      });
    } catch (e) {
      _showSnack("Error uploading file");
    } finally {
      if (mounted) setState(() => _isUploading = false);
    }
  }

  Future<void> _addSongToFirestore({
    required String title,
    required String artist,
    required String storagePath,
  }) async {
    final FirebaseFirestore firestore = FirebaseFirestore.instance;

    // Prevent duplicates by storagePath
    final QuerySnapshot<Map<String, dynamic>> existing = await firestore
        .collection(_songsCollection)
        .where("uploadedStoragePath", isEqualTo: storagePath)
        .limit(1)
        .get();

    if (existing.docs.isNotEmpty) {
      return;
    }

    // Minimal doc that works with your SongScreen:
    // - title / artist
    // - difficulties with at least one option + hands + storagePath
    await firestore.collection(_songsCollection).add({
      'title': title,
      'name': title,
      'artist': artist,
       'genre': 'User Upload',
      'source': 'user',
      'createdAt': FieldValue.serverTimestamp(),

      // Keep the original path too (nice for debugging/list screens)
      'uploadedStoragePath': storagePath,

      // SongScreen expects difficulties->hands->storagePath
      'difficulties': {
         'UNKNOWN': {
          'hands': {
      'BOTH':  {'storagePath': storagePath},
      'LEFT':  {'storagePath': storagePath},
      'RIGHT': {'storagePath': storagePath},
    },
        },
      },
    });
  }

  Future<void> _openBasicPitchDemo() async {
    final Uri url = Uri.parse('https://basicpitch.spotify.com/');
    if (!await launchUrl(url, mode: LaunchMode.externalApplication)) {
      _showSnack("Could not open link");
    }
  }

  void _showSnack(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  @override
  Widget build(BuildContext context) {
    final double boxHeight = MediaQuery.of(context).size.height * 0.65;

    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),
      body: SafeArea(
        child: SingleChildScrollView(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                const MyHeader(title: 'Upload'),
                const SizedBox(height: 24),

                DottedBorder(
                  options: const RoundedRectDottedBorderOptions(
                    color: Colors.white70,
                    strokeWidth: 2,
                    dashPattern: [15, 15],
                    radius: Radius.circular(16),
                  ),
                  child: Container(
                    padding: const EdgeInsets.all(16),
                    width: double.infinity,
                    height: boxHeight,
                    decoration: BoxDecoration(
                      color: const Color.fromARGB(255, 54, 54, 54),
                      borderRadius: BorderRadius.circular(16),
                    ),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Icon(
                          Icons.cloud_upload,
                          size: 100,
                          color: Colors.white70,
                        ),
                        const SizedBox(height: 12),
                        Text(
                          _selectedFile != null
                              ? 'Selected File: ${_selectedFile!.path.split('/').last}'
                              : 'Select a MIDI file (.mid / .midi)',
                          textAlign: TextAlign.center,
                          style: const TextStyle(
                            color: Colors.white,
                            fontSize: 16,
                          ),
                        ),
                        const SizedBox(height: 80),

                        if (_isUploading)
                          const Padding(
                            padding: EdgeInsets.all(12.0),
                            child: CircularProgressIndicator(strokeWidth: 3),
                          )
                        else
                          MyButton(
                            title: 'Browse',
                            color: Colors.blueAccent,
                            onPressed: _pickFile,
                          ),
                      ],
                    ),
                  ),
                ),

                const SizedBox(height: 40),

                ElevatedButton(
                  onPressed: _openBasicPitchDemo,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.greenAccent,
                    padding: const EdgeInsets.symmetric(
                      horizontal: 30,
                      vertical: 14,
                    ),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(4),
                    ),
                  ),
                  child: const Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.link, color: Colors.black, size: 22),
                      SizedBox(width: 10),
                      Text(
                        'Open mp3 to MIDI Conversion Tool',
                        style: TextStyle(color: Colors.black, fontSize: 16),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 1),
    );
  }
}
