// sverity-args: --top top
// Near-miss of boundary_unsized_compare: the literal is sized to 64 bits, so the comparison has no unsized literal (exempt).
// expect-none: LNT-074
module top(input logic [63:0] w, output logic h);
  assign h = (w == 64'd5);
endmodule
