// sverity-args: --top top
// A bitwise operation of two operands of one width computes on bit patterns, whatever their types: an enum xor a vector is INFO.
// expect: LNT-005 INFO 9
// expect-none: LNT-005 WARNING
module top(input logic [3:0] raw, output logic [3:0] y);
  typedef enum logic [3:0] {Lo = 4'h5, Hi = 4'ha} level_e;
  level_e lvl;
  assign lvl = Lo;
  assign y = lvl ^ raw;
endmodule
