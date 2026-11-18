// sverity-args: --top top
// A conversion to a 2-state type changes a 4-state z to 0: bit'(1'bz) is 1'b0, not z, so it is a WARNING. A conversion to
// a 4-state type (logic'(1'bz)) keeps the z and stays the tri-state INFO.
// expect: LNT-039 WARNING 7
// expect: LNT-039 INFO 8
module top(output wire w1, output wire w2);
  assign w1 = bit'(1'bz);
  assign w2 = logic'(1'bz);
endmodule
