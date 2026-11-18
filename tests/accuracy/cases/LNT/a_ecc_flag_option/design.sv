// Read-data stage with an optional parity check enabled by the HAS_ECC build option.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        rd_valid,
  input  logic [8:0]  rd_word,
  output logic        data_valid,
  output logic [7:0]  data
);
  logic       ecc_ok;
  logic [7:0] data_q;
  logic       valid_q;

`ifdef HAS_ECC
  assign ecc_ok = ~(^rd_word);
`endif

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      data_q  <= 8'h00;
      valid_q <= 1'b0;
    end else begin
      valid_q <= rd_valid && ecc_ok;
      if (rd_valid) data_q <= rd_word[7:0];
    end
  end

  assign data       = data_q;
  assign data_valid = valid_q;
endmodule
