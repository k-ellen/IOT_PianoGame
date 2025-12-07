import 'package:flutter/material.dart';
import 'package:file_picker/file_picker.dart';
import 'dart:io';
import 'package:flutter_app/widgets/header/my_header.dart';
import 'package:flutter_app/widgets/my_button.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import 'package:dotted_border/dotted_border.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';
import 'package:url_launcher/url_launcher.dart';

class SongScreen extends StatelessWidget {
  final String storagePath;

  const SongScreen({super.key, required this.storagePath});

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
            children: [MyHeader(title: 'Song')],
          ),
        ),
      ),

      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 3),
    );
  }
}
