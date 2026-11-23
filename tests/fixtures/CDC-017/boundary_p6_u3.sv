// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rsta, rstb, inc, sclr, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2, bnext, gnext;
  logic g; always_ff @(posedge clkb) g <= sclr;
  assign bnext = inc ? bin + 4'd1 : bin;
  assign gnext = bnext ^ (bnext >> 1);
  always_ff @(posedge clka or posedge rsta) begin
    if (rsta) begin bin <= 0; gray <= 0; end
    else begin bin <= bnext; gray <= gnext; end
  end
  always_ff @(posedge clkb) begin
    s1 <= gray; s2 <= s1;
    if (g) s2 <= gray;
    if (rstb) begin s1 <= 0; s2 <= 0; end
  end
  assign o = s2;
endmodule
