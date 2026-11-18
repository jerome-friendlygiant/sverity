// Byte-serial CRC-8 (polynomial 0x07) over a valid-qualified stream.
module crc8_step (
  input  logic [7:0] crc_i,
  input  logic [7:0] data_i,
  output logic [7:0] crc_o
);
  always_comb begin
    crc_o = crc_i ^ data_i;
    for (int k = 0; k < 8; k++) begin
      crc_o = crc_o[7] ? ({crc_o[6:0], 1'b0} ^ 8'h07) : {crc_o[6:0], 1'b0};
    end
  end
endmodule

module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       valid,
  input  logic       clear,
  input  logic [7:0] data,
  output logic [7:0] crc
);
  logic [7:0] crc_q;
  logic [7:0] crc_n;

  crc8_step u_step (
    .crc_i  (crc_q),
    .data_i (data),
    .en_i   (valid),
    .crc_o  (crc_n)
  );

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      crc_q <= 8'h00;
    else if (clear)  crc_q <= 8'h00;
    else if (valid)  crc_q <= crc_n;
  end

  assign crc = crc_q;
endmodule
