import 'package:flutter/material.dart';
import 'package:file_picker/file_picker.dart';
import 'dart:io';
import 'package:flutter_app/widgets/header/my_header.dart';
import 'package:flutter_app/widgets/my_button.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import 'package:dotted_border/dotted_border.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';

class UploadScreen extends StatefulWidget {
  const UploadScreen({super.key});

  @override
  _UploadScreenState createState() => _UploadScreenState();
}

class _UploadScreenState extends State<UploadScreen> {
  File? _selectedFile;
  bool _isUploading = false;

  Future<void> _pickFile() async {
    FilePickerResult? result = await FilePicker.platform.pickFiles();

    if (result != null) {
      File file = File(result.files.single.path!);

      // Ensure file extension is .mid
      final ext = file.path.split('.').last.toLowerCase();
      if (ext != 'mid') {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("Please select a .mid MIDI file")),
        );
        return;
      }

      setState(() {
        _selectedFile = File(result.files.single.path!);
      });

      _uploadFile();
    }
  }

  Future<void> _uploadFile() async {
    if (_selectedFile == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Please select a MIDI file first")),
      );
      return;
    }

    try {
      setState(() => _isUploading = true);

      final fileName = _selectedFile!.path.split('/').last;
      final storageRef = FirebaseStorage.instance.ref().child(
        "uploadedSongs/$fileName",
      );

      await storageRef.putFile(_selectedFile!);

      final downloadUrl = await storageRef.getDownloadURL();
      print("Uploaded successfully! URL: $downloadUrl");

      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("File uploaded successfully!")),
      );
      setState(() {
        _selectedFile = null;
      });
    } catch (e) {
      print("Upload error: $e");
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(const SnackBar(content: Text("Error uploading file")));
    } finally {
      setState(() => _isUploading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),

      body: SingleChildScrollView(
        //allows scrolling of all content if there is not enough height
        child: Padding(
          padding: const EdgeInsets.all(16.0), //adds space around all content.
          child: Column(
            //starting from top to bottom
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              MyHeader(title: 'Upload'),

              const SizedBox(height: 24), //space
              // Upload Area
              DottedBorder(
                options: RoundedRectDottedBorderOptions(
                  color: Colors.white70,
                  strokeWidth: 2,
                  dashPattern: [15, 15],
                  radius: Radius.circular(16),
                ),
                child: Container(
                  padding: const EdgeInsets.all(16),
                  width: double.infinity,
                  height: MediaQuery.of(context).size.height * 0.75,
                  decoration: BoxDecoration(
                    color: const Color.fromARGB(255, 54, 54, 54),
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(
                        Icons.cloud_upload,
                        size: 100,
                        color: Colors.white70,
                      ),

                      Text(
                        _selectedFile != null
                            ? 'Selected File: ${_selectedFile!.path.split('/').last}'
                            : 'select audio file (.mid)',
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 16,
                        ),
                      ),

                      const SizedBox(height: 80),

                      _isUploading
                          ? const Padding(
                              padding: EdgeInsets.all(12.0),
                              child: CircularProgressIndicator(
                                strokeWidth: 3,
                                color: Colors.blueAccent,
                              ),
                            )
                          : MyButton(
                              title: 'Browse',
                              color: Colors.blueAccent,
                              onPressed: _pickFile,
                            ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 40), //space
              // _isUploading
              //     ? const Padding(
              //         padding: EdgeInsets.all(12.0),
              //         child: CircularProgressIndicator(
              //           strokeWidth: 3,
              //           color: Colors.greenAccent,
              //         ),
              //       )
              //     : MyButton(
              //         title: 'Upload',
              //         color: Colors.greenAccent,
              //         onPressed: () => _uploadFile(),
              //       ),
            ],
          ),
        ),
      ),

      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 2),
    );
  }
}
