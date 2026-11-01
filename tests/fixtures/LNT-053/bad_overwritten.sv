// sverity-args: --top top
// t = a is overwritten by t = b before any read, in one straight-line block
// expect: LNT-053 WARNING 7
module top(input logic a, b, output logic y);
  logic t;
  always_comb begin
    t = a;
    t = b;
    y = t;
  end
endmodule
