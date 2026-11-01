// sverity-args: --top top
// audit: @(m[0]) is sensitive to bit 0 only; the body reads m[1], which is not in the list
// expect: LNT-045 WARNING
module top(input logic a, output logic y);
  logic [1:0] m;
  always @(m[0] or a) begin
    y = m[1] & a;
  end
endmodule
