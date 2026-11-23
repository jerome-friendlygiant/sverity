// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-none: CDC-017
module lane_sync(input logic clk_i, rst_ni, data_i, output logic data_o);
  logic first_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin first_q <= 1'b0; data_o <= 1'b0; end
    else begin first_q <= data_i; data_o <= first_q; end
endmodule

module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] source;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) source <= '0;
    else if (inc) source <= source + 1'b1;
  // Source bit 2 is captured twice and bit 3 is absent.
  for (genvar i = 0; i < 4; ++i) begin : gen_duplicate
    if (i == 3) lane_sync u_lane(clkb, rst_n, source[2], q[i]);
    else lane_sync u_lane(clkb, rst_n, source[i], q[i]);
  end
endmodule
