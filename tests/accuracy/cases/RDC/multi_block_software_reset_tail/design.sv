module top (
  input  logic clk_i,
  input  logic por_ni,
  input  logic software_reset_i,
  input  logic request_i,
  output logic response_o
);
  logic [1:0] por_pipe_q;
  logic       release_ni;
  logic       block_reset_ni;
  logic [3:0] engine_q;
  logic [3:0] monitor_q;

  // The POR release is synchronized, but the software reset is appended later.
  always_ff @(posedge clk_i or negedge por_ni) begin
    if (!por_ni)
      por_pipe_q <= 2'b00;
    else
      por_pipe_q <= {por_pipe_q[0], 1'b1};
  end

  assign release_ni = por_pipe_q[1];
  assign block_reset_ni = release_ni & ~software_reset_i;

  engine u_engine (
    .clk_i(clk_i), .reset_ni(block_reset_ni),
    .request_i(request_i), .value_o(engine_q)
  );

  monitor u_monitor (
    .clk_i(clk_i), .reset_ni(block_reset_ni),
    .value_i(engine_q), .value_o(monitor_q)
  );

  always_comb begin
    response_o = (monitor_q == 4'h5);
  end
endmodule

module engine (
  input logic clk_i, input logic reset_ni, input logic request_i,
  output logic [3:0] value_o
);
  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) value_o <= 4'h0;
    else if (request_i) value_o <= value_o + 4'h1;
  end
endmodule

module monitor (
  input logic clk_i, input logic reset_ni, input logic [3:0] value_i,
  output logic [3:0] value_o
);
  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) value_o <= 4'h0;
    else value_o <= value_i;
  end
endmodule
