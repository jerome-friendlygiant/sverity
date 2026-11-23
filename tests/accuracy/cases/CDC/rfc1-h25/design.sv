// Byte pipeline split over two modules; the second takes a local clock alias.
module stage_a (
  input  logic       clk,
  input  logic [7:0] d_i,
  output logic [7:0] q_o
);
  always_ff @(posedge clk) q_o <= d_i;
endmodule

module stage_b (
  input  logic       clk_a,
  input  logic       rst_ni,
  input  logic [7:0] d_i,
  output logic [7:0] q_o
);
  wire clk_alias = clk_a;

  always_ff @(posedge clk_alias) begin
    if (!rst_ni) q_o <= 8'h00;
    else         q_o <= d_i;
  end
endmodule

module top (
  input  logic       clk_a,
  input  logic       rst_ni,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic [7:0] mid;

  stage_a u_a (.clk(clk_a), .d_i(data_i), .q_o(mid));
  stage_b u_b (.clk_a(clk_a), .rst_ni(rst_ni), .d_i(mid), .q_o(data_o));
endmodule
