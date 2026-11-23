// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-count: CDC-017 ERROR 2
// expect-confidence: CDC-017 heuristic
// expect-none: CDC-009
// An outer generate instance fixes the row; only the inner lane index varies.
module lane_sync(input logic clk_i, rst_ni, data_i, output logic data_o);
  logic first_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin first_q <= 1'b0; data_o <= 1'b0; end
    else begin first_q <= data_i; data_o <= first_q; end
endmodule

module channel(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else if (inc) bin <= bin + 1'b1;
  for (genvar i = 0; i < 4; ++i) begin : gen_sync
    lane_sync u_lane(clkb, rst_n, bin[i], q[i]);
  end
endmodule

module top(input logic clka, clkb, rst_n, inc, output logic [7:0] q);
  for (genvar row = 0; row < 2; ++row) begin : gen_row
    channel u_channel(clka, clkb, rst_n, inc, q[4*row +: 4]);
  end
endmodule
