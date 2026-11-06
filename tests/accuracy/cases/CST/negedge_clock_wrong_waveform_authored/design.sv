module ddr_capture #(parameter int WIDTH = 8) (
  input logic clk_i,
  input logic rst_ni,
  input logic valid_i,
  input logic [WIDTH-1:0] data_i,
  output logic [WIDTH-1:0] data_o,
  output logic valid_o
);
  logic [WIDTH-1:0] data_q;
  logic valid_q;
  always_ff @(negedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      data_q <= '0;
      valid_q <= 1'b0;
    end else begin
      data_q <= data_i;
      valid_q <= valid_i;
    end
  end
  assign data_o = data_q;
  assign valid_o = valid_q;
endmodule

module top (
  input logic clk_i,
  input logic rst_ni,
  input logic valid_i,
  input logic [7:0] data_i,
  output logic [7:0] data_o,
  output logic valid_o
);
  ddr_capture #(.WIDTH(8)) u_capture (.*);
endmodule
