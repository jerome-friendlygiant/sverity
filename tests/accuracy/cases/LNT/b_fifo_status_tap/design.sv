// Occupancy tracker for a 16-entry FIFO.
module occ_counter (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       push,
  input  logic       pop,
  output logic [4:0] count,
  output logic       full,
  output logic       empty,
  output logic       almost_full
);
  assign full        = (count == 5'd16);
  assign empty       = (count == 5'd0);
  assign almost_full = (count >= 5'd14);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) count <= 5'd0;
    else begin
      case ({push & ~full, pop & ~empty})
        2'b10:   count <= count + 5'd1;
        2'b01:   count <= count - 5'd1;
        default: count <= count;
      endcase
    end
  end
endmodule

// Read-side monitor: only the fill level and the empty flag are used here.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       push,
  input  logic       pop_req,
  input  logic       sink_ready,
  output logic [4:0] level,
  output logic       rd_fire
);
  logic fifo_empty;

  assign rd_fire = pop_req & sink_ready & ~fifo_empty;

  occ_counter u_occ (
    .clk(clk),
    .rst_n(rst_n),
    .push(push),
    .pop(rd_fire),
    .count(level),
    .full(),
    .empty(fifo_empty),
    .almost_full()
  );
endmodule
