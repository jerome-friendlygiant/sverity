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

module top(input logic clka, clkb, rst_n, inc_a, inc_b, output logic [3:0] q);
  logic [3:0] source_a, source_b;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin source_a <= '0; source_b <= '0; end
    else begin
      if (inc_a) source_a <= source_a + 1'b1;
      if (inc_b) source_b <= source_b + 1'b1;
    end
  // The final lane comes from a different source vector, so this is not one Gray bus.
  for (genvar i = 0; i < 4; ++i) begin : gen_mixed
    if (i == 3) lane_sync u_lane(clkb, rst_n, source_b[i], q[i]);
    else lane_sync u_lane(clkb, rst_n, source_a[i], q[i]);
  end
endmodule
