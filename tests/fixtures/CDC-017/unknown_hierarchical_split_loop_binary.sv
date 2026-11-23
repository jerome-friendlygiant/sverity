// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-none: CDC-017
// Complementary source-bit ranges are captured under separate generate scopes.
module lane_sync(input logic clk_i, rst_ni, data_i, output logic data_o);
  logic first_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin first_q <= 1'b0; data_o <= 1'b0; end
    else begin first_q <= data_i; data_o <= first_q; end
endmodule

module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else if (inc) bin <= bin + 1'b1;
  for (genvar i = 0; i < 2; ++i) begin : gen_low
    lane_sync u_lane(clkb, rst_n, bin[i], q[i]);
  end
  for (genvar i = 2; i < 4; ++i) begin : gen_high
    lane_sync u_lane(clkb, rst_n, bin[i], q[i]);
  end
endmodule
