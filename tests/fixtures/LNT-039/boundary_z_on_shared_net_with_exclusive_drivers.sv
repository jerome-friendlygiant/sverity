// sverity-args: --top top
// Two drivers of one net with exclusive enables are a bus: each 'z is the other's turn, so INFO.
// expect: LNT-039 INFO 7
// expect-none: LNT-039 WARNING
module top(input logic ea, eb, a, b, output logic y);
  wire bus;
  assign bus = ea ? a : 1'bz;
  assign bus = eb ? b : 1'bz;
  assign y = bus;
endmodule
