// Places an 8-bit payload into one byte lane of a 32-bit write word.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        wr_en,
  input  logic [1:0]  lane,
  input  logic [7:0]  payload,
  output logic        bus_valid,
  output logic [31:0] bus_word
);
  logic [31:0] word_q;
  logic        valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      word_q  <= 32'h0;
      valid_q <= 1'b0;
    end else begin
      valid_q <= wr_en;
      if (wr_en) word_q <= {24'd0, payload << {lane, 3'b000}};
    end
  end

  assign bus_word  = word_q;
  assign bus_valid = valid_q;
endmodule
