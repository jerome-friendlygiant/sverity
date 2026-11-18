// Six-entry slot table with a range-checked write and read.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       wr_en,
  input  logic [2:0] slot,
  input  logic [7:0] wdata,
  output logic [7:0] rdata
);
  localparam int N_SLOTS = 6;

  logic [7:0] slots_q [N_SLOTS];
  logic       slot_ok;

  assign slot_ok = (slot < 3'd6);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      for (int i = 0; i < N_SLOTS; i++) slots_q[i] <= 8'h00;
    end else if (wr_en && slot_ok) begin
      slots_q[slot] <= wdata;
    end
  end

  assign rdata = slot_ok ? slots_q[slot] : 8'h00;
endmodule
