// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        req_i,
  input  logic [5:0]  addr_i,
  output logic [15:0] coef_o,
  output logic        coef_valid_o
);
  localparam logic [15:0] COEF_ROM [12] = '{
    16'h0100, 16'h01C3, 16'h0289, 16'h034F,
    16'h0412, 16'h04D8, 16'h059E, 16'h0661,
    16'h0727, 16'h07EB, 16'h08B0, 16'h0974
  };

  logic [3:0]  bank_sel;
  logic [15:0] coef_q;
  logic        valid_q;

  vendor_bank_decode u_dec (
    .addr (addr_i),
    .bank (bank_sel)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      coef_q  <= '0;
      valid_q <= 1'b0;
    end else begin
      valid_q <= req_i;
      if (req_i) coef_q <= COEF_ROM[bank_sel];
    end
  end

  assign coef_o       = coef_q;
  assign coef_valid_o = valid_q;
endmodule
