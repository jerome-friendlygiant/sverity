module top (
  input  logic clk_core_i,
  input  logic clk_io_i,
  input  logic ext_rst_ni,
  input  logic [7:0] command_i,
  output logic [7:0] result_o
);
  logic [1:0] io_release_q;
  logic       io_reset_ni;
  logic [7:0] state_q;

  // The IO clock is not the clock of the block receiving this reset.
  always_ff @(posedge clk_io_i or negedge ext_rst_ni) begin
    if (!ext_rst_ni) io_release_q <= 2'b00;
    else io_release_q <= {io_release_q[0], 1'b1};
  end
  assign io_reset_ni = io_release_q[1];

  always_ff @(posedge clk_core_i or negedge io_reset_ni) begin
    if (!io_reset_ni) state_q <= 8'h00;
    else state_q <= command_i + 8'h20;
  end

  assign result_o = state_q;
endmodule
