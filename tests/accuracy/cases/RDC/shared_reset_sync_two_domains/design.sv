module top (
  input  logic clk_a_i,
  input  logic clk_b_i,
  input  logic global_rst_ni,
  input  logic [3:0] data_a_i,
  input  logic [3:0] data_b_i,
  output logic [3:0] data_a_o,
  output logic [3:0] data_b_o
);
  logic [1:0] shared_pipe_q;
  logic       shared_rst_ni;

  // A reset synchronizer is a clock-domain-local circuit.
  always_ff @(posedge clk_a_i or negedge global_rst_ni) begin
    if (!global_rst_ni) shared_pipe_q <= 2'b00;
    else shared_pipe_q <= {shared_pipe_q[0], 1'b1};
  end
  assign shared_rst_ni = shared_pipe_q[1];

  domain_a u_a (
    .clk_i(clk_a_i), .reset_ni(shared_rst_ni),
    .data_i(data_a_i), .data_o(data_a_o)
  );
  domain_b u_b (
    .clk_i(clk_b_i), .reset_ni(shared_rst_ni),
    .data_i(data_b_i), .data_o(data_b_o)
  );
endmodule

module domain_a (
  input logic clk_i, input logic reset_ni, input logic [3:0] data_i,
  output logic [3:0] data_o
);
  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) data_o <= 4'h0;
    else data_o <= data_i + 4'h1;
  end
endmodule

module domain_b (
  input logic clk_i, input logic reset_ni, input logic [3:0] data_i,
  output logic [3:0] data_o
);
  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) data_o <= 4'h0;
    else data_o <= data_i ^ 4'h3;
  end
endmodule
