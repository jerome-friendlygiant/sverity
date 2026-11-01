// sverity-args: --top top
// a nonblocking assignment in always_comb delays the update past the block
// expect: LNT-062 WARNING 6
module top(input logic a, b, output logic y);
  always_comb begin
    y <= a & b;
  end
endmodule
