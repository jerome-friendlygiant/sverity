// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rst_n, inc, guard, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) bin <= 0;
    else bin <= bin + inc;
  end
  assign gray = bin ^ (bin >> 1);
  always_ff @(posedge clkb) begin
    s1 <= gray ^ 4'b0001; s2 <= s1;
    if (guard) s1 <= 0;
  end
  assign o = s2;
endmodule
