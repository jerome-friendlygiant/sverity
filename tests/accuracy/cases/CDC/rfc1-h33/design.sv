// Registered adder macro boundary.
module top (
  input  logic       clk_io,
  input  logic       rst_ni,
  input  logic [7:0] a_i,
  input  logic [7:0] b_i,
  input  logic       valid_i,
  output logic [8:0] sum_o,
  output logic       valid_o
);
  logic [7:0] a_q;
  logic [7:0] b_q;
  logic       v_q;
  logic [8:0] sum_q;
  logic       valid_q;

  always_ff @(posedge clk_io) begin
    a_q <= a_i;
    b_q <= b_i;
    v_q <= valid_i;
  end

  always_ff @(posedge clk_io) begin
    if (!rst_ni) begin
      sum_q   <= 9'd0;
      valid_q <= 1'b0;
    end else begin
      sum_q   <= a_q + b_q;
      valid_q <= v_q;
    end
  end

  assign sum_o   = sum_q;
  assign valid_o = valid_q;
endmodule
