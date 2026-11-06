module top (
  input  logic clk_i,
  input  logic por_ni,
  input  logic pll_lock_i,
  input  logic [3:0] command_i,
  output logic [3:0] result_o
);
  logic [1:0] por_release_q;
  logic       local_reset_ni;
  logic [3:0] result_q;

  always_ff @(posedge clk_i or negedge por_ni) begin
    if (!por_ni) por_release_q <= 2'b00;
    else por_release_q <= {por_release_q[0], 1'b1};
  end

  // PLL lock is a raw analog/status indication in the reset path.
  assign local_reset_ni = por_release_q[1] & pll_lock_i;

  always_ff @(posedge clk_i or negedge local_reset_ni) begin
    if (!local_reset_ni) result_q <= 4'h0;
    else result_q <= command_i;
  end

  assign result_o = result_q;
endmodule
