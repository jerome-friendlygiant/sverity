// sverity-args: --top top
// b is listed but never read by the block
// expect: LNT-045 INFO 5
module top(input logic a, b, output logic y);
  always @(a or b) begin
    y = a;
  end
endmodule
