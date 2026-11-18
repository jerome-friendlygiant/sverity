// Selects one byte of a 24-bit sample word by lane number.
// The selected byte is registered together with a valid strobe so the
// consumer can sample it one cycle after the request.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [23:0] word_i,
  input  logic [1:0]  lane,
  input  logic        req,
  output logic        byte_valid,
  output logic [7:0]  byte_o
);
  logic [7:0] byte_q;
  logic       valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      byte_q  <= 8'h00;
      valid_q <= 1'b0;
    end else begin
      valid_q <= req;
      if (req) byte_q <= word_i[{lane, 3'b000} +: 8];
    end
  end

  assign byte_o     = byte_q;
  assign byte_valid = valid_q;
endmodule
