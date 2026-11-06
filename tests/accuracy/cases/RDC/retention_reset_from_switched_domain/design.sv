module top (
  input  logic clk_aon_i,
  input  logic clk_sw_i,
  input  logic aon_rst_ni,
  input  logic switched_rst_ni,
  input  logic [7:0] state_i,
  output logic [7:0] retained_o,
  output logic [7:0] active_o
);
  logic [1:0] switched_release_q;
  logic       switched_reset_ni;
  logic [7:0] retained_q;
  logic [7:0] active_q;

  always_ff @(posedge clk_sw_i or negedge switched_rst_ni) begin
    if (!switched_rst_ni) switched_release_q <= 2'b00;
    else switched_release_q <= {switched_release_q[0], 1'b1};
  end
  assign switched_reset_ni = switched_release_q[1];

  // Retention flops are in the always-on clock domain.
  always_ff @(posedge clk_aon_i or negedge switched_reset_ni) begin
    if (!switched_reset_ni) retained_q <= 8'h00;
    else retained_q <= state_i;
  end

  always_ff @(posedge clk_sw_i or negedge switched_reset_ni) begin
    if (!switched_reset_ni) active_q <= 8'h00;
    else active_q <= state_i ^ 8'h5a;
  end

  assign retained_o = retained_q;
  assign active_o = active_q;
endmodule
