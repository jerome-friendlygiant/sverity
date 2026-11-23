// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rsta, rstb, inc, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2, bnext;
  wire inverted_clear = ~rstb;
  assign bnext = bin + inc;
  assign gray = bnext ^ (bnext >> 1);
  always_ff @(posedge clka or posedge rsta)
    if (rsta) bin <= 0; else bin <= bnext;
  always_ff @(posedge clkb) begin
    s1 <= gray;
    s2 <= s1;
    if (rstb) s1 <= 0;
    if (inverted_clear) s1 <= 0;
  end
  assign o = s2;
endmodule
