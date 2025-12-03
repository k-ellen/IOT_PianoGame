import 'package:flutter/material.dart';
import 'package:file_picker/file_picker.dart';
import 'dart:io';
import 'package:flutter_app/widgets/header/my_header.dart';
import 'package:flutter_app/widgets/my_button.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import 'package:dotted_border/dotted_border.dart';

class UploadScreen extends StatefulWidget {
  const UploadScreen({super.key});

  @override
  _UploadScreenState createState() => _UploadScreenState();
}

class _UploadScreenState extends State<UploadScreen> {
  File? _selectedFile;

  Future<void> _pickFile() async {
    FilePickerResult? result = await FilePicker.platform.pickFiles();

    if (result != null) {
      setState(() {
        _selectedFile = File(result.files.single.path!);
      });
    } else {
      // User canceled the picker
    }
  }

  Future<void> _uploadFile() async {
    if (_selectedFile == null) {
      // Show a message to the user to select a file first
      return;
    }

    // Implement your file upload logic here
    // This could involve sending the file to a server using HTTP,
    // or uploading to cloud storage like Firebase.
    print('Uploading file: ${_selectedFile!.path}');
    // Example: You would typically use a package like http, Dio, or Firebase Storage here.
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
                  height: MediaQuery.of(context).size.height * 0.6,
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

                      MyButton(
                        title: 'Browse',
                        color: Colors.blueAccent,
                        onPressed: _pickFile,
                      ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 40), //space

              MyButton(
                title: 'Upload',
                color: Colors.greenAccent,
                onPressed: _uploadFile,
              ),
            ],
          ),
        ),
      ),

      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 2),
    );
  }
}
