// Packs four control flags into the low nibble of an 8-bit status register.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       start,
  input  logic       finish,
  input  logic       fault,
  output logic [7:0] status
);
  logic busy_q;
  logic done_q;
  logic err_q;
  logic irq_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      busy_q <= 1'b0;
      done_q <= 1'b0;
      err_q  <= 1'b0;
      irq_q  <= 1'b0;
    end else begin
      if (start)  busy_q <= 1'b1;
      if (finish) begin
        busy_q <= 1'b0;
        done_q <= 1'b1;
        irq_q  <= 1'b1;
      end
      if (fault)  err_q <= 1'b1;
    end
  end

  assign status[0] = busy_q;
  assign status[1] = done_q;
  assign status[2] = err_q;
  assign status[3] = irq_q;
endmodule
