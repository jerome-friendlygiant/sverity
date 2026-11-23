// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
module top(input logic clka, clkb, rsta, rstb, inc, sclr, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2;
  logic g; always_ff @(posedge clkb) g <= sclr;
  always_ff @(posedge clka) begin
    if (inc) begin bin <= bin + 1; gray <= (bin+4'd1) ^ ((bin+4'd1) >> 1); end
    if (rsta) begin bin <= 0; gray <= 0; end
  end
  always_ff @(posedge clkb) begin
    s1 <= gray; s2 <= s1;
    if (g) s1 <= s2;
    if (rstb) begin s1 <= 0; s2 <= 0; end
  end
  assign o = s2;
endmodule
