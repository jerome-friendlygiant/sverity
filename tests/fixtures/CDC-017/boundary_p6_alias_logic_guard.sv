// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-017
// expect: CDC-018 WARNING
module top(input logic clka, clkb, rsta, rstb, inc, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2, bnext, gnext;
  logic r1, r2, f1, f2;
  wire alias_reset = r2;
  wire local_reset = alias_reset | f2;
  assign bnext = inc ? bin + 4'd1 : bin;
  assign gnext = bnext ^ (bnext >> 1);
  always_ff @(posedge clka or posedge rsta) begin
    if (rsta) begin bin <= 0; gray <= 0; end
    else begin bin <= bnext; gray <= gnext; end
  end
  always_ff @(posedge clkb) begin
    r1 <= rstb; r2 <= r1;
    f1 <= inc; f2 <= f1;
  end
  always_ff @(posedge clkb) begin
    if (local_reset) begin s1 <= 0; s2 <= 0; end
    else begin s1 <= gray; s2 <= s1; end
  end
  assign o = s2;
endmodule
