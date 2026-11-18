// sverity-args: --top top
// A macro call inside a final block is inside the final block: its force is INFO (advisory), at the call line.
// expect: LNT-079 INFO 10
// expect-none: LNT-079 WARNING
`define FORCE0(s) force s = 1'b0
module top(input logic a, output logic y);
  logic r;
  assign y = r;
  final begin
    `FORCE0(r);
  end
endmodule
