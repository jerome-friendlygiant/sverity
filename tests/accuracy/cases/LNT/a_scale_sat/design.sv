// Unsigned 8x8 scaler that saturates the result to 8 bits.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       in_valid,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic       out_valid,
  output logic [7:0] y
);
  logic [15:0] prod;
  logic        ovf;
  logic [7:0]  y_q;
  logic        valid_q;

  assign prod = a * b;
  assign ovf  = |prod[15:8];

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      y_q     <= 8'h00;
      valid_q <= 1'b0;
    end else begin
      valid_q <= in_valid;
      if (in_valid) y_q <= ovf ? 8'hFF : prod[7:0];
    end
  end

  assign y         = y_q;
  assign out_valid = valid_q;
endmodule
