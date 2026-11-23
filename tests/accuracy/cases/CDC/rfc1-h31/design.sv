// Enabled pipeline register using a synchronous clock enable.
module top (
  input  logic       clk_a,
  input  logic       sync_rst_ni,
  input  logic       en_i,
  input  logic [7:0] d_i,
  output logic [7:0] q_o
);
  logic       pipe_en_q;
  logic [7:0] stage_in_q;
  logic [7:0] data_q;

  always_ff @(posedge clk_a) begin
    pipe_en_q  <= en_i;
    stage_in_q <= d_i;
  end

  always_ff @(posedge clk_a) begin
    if (!sync_rst_ni)    data_q <= 8'h00;
    else if (pipe_en_q) data_q <= stage_in_q;
  end

  assign q_o = data_q;
endmodule
