// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-004 ERROR
// expect-none: CDC-028 ERROR
// Vector synchroniser with an unconditional pipeline followed by a reset override.
module top(input logic clka, clkb, rst_b, enable, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] src, stage1, stage2;
  logic unrelated;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin
    stage1 <= src; stage2 <= stage1;
    if (enable) unrelated <= d[0];
    if (rst_b) begin stage1 <= '0; stage2 <= '0; end
  end
  assign q = stage2;
endmodule
