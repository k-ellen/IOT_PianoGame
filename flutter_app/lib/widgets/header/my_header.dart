import 'package:flutter/material.dart';

class MyHeader extends StatelessWidget {
  final String title;
  final bool isBackButton;

  const MyHeader({super.key, required this.title, this.isBackButton = false});

  @override
  Widget build(BuildContext context) {
    return Row(
      //In the top row there is the word search and the icon
      mainAxisAlignment: MainAxisAlignment
          .spaceBetween, //One is pushed to the left edge the other to the right edge
      children: [
        if (isBackButton)
          IconButton(
            icon: const Icon(Icons.arrow_back, color: Colors.white),
            onPressed: () {
              Navigator.pop(context);
            },
          ),
        Text(
          title,
          style: const TextStyle(
            color: Colors.white,
            fontSize: 32,
            fontWeight: FontWeight.bold,
          ),
        ),
        ClipRRect(
          //Shape with rounded corners
          borderRadius: BorderRadius.circular(12),
          child: Image.asset(
            'assets/images/logo.png',
            width: 40,
            height: 40,
            fit: BoxFit.cover,
          ),
        ),
      ],
    );
  }
}
