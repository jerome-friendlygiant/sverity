// Two-stage byte pipeline with a buffered clock wire.
module bus_stage (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] d_i,
  output logic [7:0] q_o
);
  always_ff @(posedge clk) begin
    if (!rst_n) q_o <= 8'h00;
    else        q_o <= d_i;
  end
endmodule

module top (
  input  logic       clk_sys,
  input  logic       sync_rst_n,
  input  logic [7:0] d_i,
  output logic [7:0] q_o
);
  wire        clk_buf = clk_sys;
  logic [7:0] mid;

  bus_stage u_s0 (.clk(clk_buf), .rst_n(sync_rst_n), .d_i(d_i), .q_o(mid));
  bus_stage u_s1 (.clk(clk_buf), .rst_n(sync_rst_n), .d_i(mid), .q_o(q_o));
endmodule
