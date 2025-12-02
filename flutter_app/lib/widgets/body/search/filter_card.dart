import 'package:flutter/material.dart';

export 'filter_card.dart';

class FilterCard extends StatelessWidget {
  ////draws one square from the filter squares
  final Color color;
  final String label;
  final bool centerBig;

  const FilterCard({
    required this.color,
    required this.label,
    this.centerBig = false,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Center(
        child: Text(
          label,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: Colors.black,
            fontSize: centerBig ? 26 : 16,
            fontWeight: FontWeight.bold,
          ),
        ),
      ),
    );
  }
}
