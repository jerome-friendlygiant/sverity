// sverity-args: --top top
// b is read by the block but is not in the level-sensitive list: the block does not re-run when b changes
// expect: LNT-045 WARNING 5
module top(input logic a, b, output logic y);
  always @(a) begin
    y = a & b;
  end
endmodule
