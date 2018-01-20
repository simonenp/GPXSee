#include "latlon.h"
#include "utm.h"
#include "gcs.h"
#include "mapfile.h"


static double parameter(const QString &str, bool *res)
{
	QString field = str.trimmed();
	if (field.isEmpty()) {
		*res = true;
		return NAN;
	}

	return field.toDouble(res);
}

int MapFile::parse(QIODevice &device, QList<CalibrationPoint> &points,
  QString &projection, Projection::Setup &setup, QString &datum)
{
	bool res, r[8];
	int ln = 1;

	while (!device.atEnd()) {
		QByteArray line = device.readLine();

		if (ln == 1) {
			if (!line.trimmed().startsWith("OziExplorer Map Data File"))
				return ln;
		} else if (ln == 2)
			_name = line.trimmed();
		else if (ln == 3)
			_image = line.trimmed();
		else if (ln == 5)
			datum = line.split(',').at(0).trimmed();
		else {
			QList<QByteArray> list = line.split(',');
			QString key(list.at(0).trimmed());
			bool ll = true; bool pp = true;


			if (key.startsWith("Point") && list.count() == 17
			  && !list.at(2).trimmed().isEmpty()) {
				CalibrationPoint p;

				int x = list.at(2).trimmed().toInt(&res);
				if (!res)
					return ln;
				int y = list.at(3).trimmed().toInt(&res);
				if (!res)
					return ln;

				int latd = list.at(6).trimmed().toInt(&res);
				if (!res)
					ll = false;
				qreal latm = list.at(7).trimmed().toFloat(&res);
				if (!res)
					ll = false;
				int lond = list.at(9).trimmed().toInt(&res);
				if (!res)
					ll = false;
				qreal lonm = list.at(10).trimmed().toFloat(&res);
				if (!res)
					ll = false;
				if (ll && list.at(8).trimmed() == "S") {
					latd = -latd;
					latm = -latm;
				}
				if (ll && list.at(11).trimmed() == "W") {
					lond = -lond;
					lonm = -lonm;
				}

				p.zone = list.at(13).trimmed().toInt(&res);
				if (!res)
					p.zone = 0;
				qreal ppx = list.at(14).trimmed().toFloat(&res);
				if (!res)
					pp = false;
				qreal ppy = list.at(15).trimmed().toFloat(&res);
				if (!res)
					pp = false;
				if (list.at(16).trimmed() == "S")
					p.zone = -p.zone;

				p.rp.xy = QPoint(x, y);
				if (ll) {
					p.ll = Coordinates(lond + lonm/60.0, latd + latm/60.0);
					if (p.ll.isValid())
						points.append(p);
					else
						return ln;
				} else if (pp) {
					p.rp.pp = QPointF(ppx, ppy);
					points.append(p);
				} else
					return ln;
			} else if (key == "IWH") {
				if (list.count() < 4)
					return ln;
				int w = list.at(2).trimmed().toInt(&res);
				if (!res)
					return ln;
				int h = list.at(3).trimmed().toInt(&res);
				if (!res)
					return ln;
				_size = QSize(w, h);
			} else if (key == "Map Projection") {
				if (list.count() < 2)
					return ln;
				projection = list.at(1);
			} else if (key == "Projection Setup") {
				if (list.count() < 8)
					return ln;

				setup = Projection::Setup(
				  parameter(list[1], &r[1]), parameter(list[2], &r[2]),
				  parameter(list[3], &r[3]), parameter(list[4], &r[4]),
				  parameter(list[5], &r[5]), parameter(list[6], &r[6]),
				  parameter(list[7], &r[7]));

				for (int i = 1; i < 8; i++)
					if (!r[i])
						return ln;
			}
		}

		ln++;
	}

	return 0;
}

bool MapFile::parseMapFile(QIODevice &device, QList<CalibrationPoint> &points,
  QString &projection, Projection::Setup &setup, QString &datum)
{
	int el;

	if (!device.open(QIODevice::ReadOnly)) {
		_errorString = QString("Error opening map file: %1")
		  .arg(device.errorString());
		return false;
	}

	if ((el = parse(device, points, projection, setup, datum))) {
		_errorString = QString("Map file parse error on line %1").arg(el);
		return false;
	}

	return true;
}

bool MapFile::createDatum(const QString &datum)
{
	if (!(_gcs = GCS::gcs(datum))) {
		_errorString = QString("%1: Unknown datum").arg(datum);
		return false;
	}

	return true;
}

bool MapFile::createProjection(const QString &name,
  const Projection::Setup &setup, QList<CalibrationPoint> &points)
{
	if (name == "Mercator")
		_projection = Projection::projection(_gcs->datum(), 1024, setup);
	else if (name == "Transverse Mercator")
		_projection = Projection::projection(_gcs->datum(), 9807, setup);
	else if (name == "Latitude/Longitude")
		_projection = new LatLon(9102);
	else if (name == "Lambert Conformal Conic")
		_projection = Projection::projection(_gcs->datum(), 9802, setup);
	else if (name == "Albers Equal Area")
		_projection = Projection::projection(_gcs->datum(), 9822, setup);
	else if (name == "(A)Lambert Azimuthual Equal Area")
		_projection = Projection::projection(_gcs->datum(), 9820, setup);
	else if (name == "(UTM) Universal Transverse Mercator") {
		int zone;
		if (points.first().zone)
			zone = points.first().zone;
		else if (!points.first().ll.isNull())
			zone = UTM::zone(points.first().ll);
		else {
			_errorString = "Can not determine UTM zone";
			return 0;
		}
		_projection = Projection::projection(_gcs->datum(), 9807,
		  UTM::setup(zone));
	} else if (name == "(NZTM2) New Zealand TM 2000")
		_projection = Projection::projection(_gcs->datum(), 9807,
		  Projection::Setup(0, 173.0, 0.9996, 1600000, 10000000, NAN, NAN));
	else if (name == "(BNG) British National Grid")
		_projection = Projection::projection(_gcs->datum(), 9807,
		  Projection::Setup(49, -2, 0.999601, 400000, -100000, NAN, NAN));
	else if (name == "(IG) Irish Grid")
		_projection = Projection::projection(_gcs->datum(), 9807,
		  Projection::Setup(53.5, -8, 1.000035, 200000, 250000, NAN, NAN));
	else if (name == "(SG) Swedish Grid")
		_projection = Projection::projection(_gcs->datum(), 9807,
		  Projection::Setup(0, 15.808278, 1, 1500000, 0, NAN, NAN));
	else if (name == "(I) France Zone I")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(49.5, 2.337229, NAN, 600000, 1200000, 48.598523,
		  50.395912));
	else if (name == "(II) France Zone II")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(46.8, 2.337229, NAN, 600000, 2200000, 45.898919,
		  47.696014));
	else if (name == "(III) France Zone III")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(44.1, 2.337229, NAN, 600000, 3200000, 43.199291,
		  44.996094));
	else if (name == "(IV) France Zone IV")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(42.165, 2.337229, NAN, 234.358, 4185861.369,
		  41.560388, 42.767663));
	else if (name == "(VICGRID) Victoria Australia")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(-37, 145, NAN, 2500000, 4500000, -36, -38));
	else if (name == "(VG94) VICGRID94 Victoria Australia")
		_projection = Projection::projection(_gcs->datum(), 9802,
		  Projection::Setup(-37, 145, NAN, 2500000, 2500000, -36, -38));
	else {
		_errorString = QString("%1: Unknown map projection").arg(name);
		return false;
	}

	return true;
}

bool MapFile::computeTransformation(QList<CalibrationPoint> &points)
{
	QList<ReferencePoint> rp;

	for (int i = 0; i < points.size(); i++) {
		if (points.at(i).rp.pp.isNull())
			points[i].rp.pp = _projection->ll2xy(points.at(i).ll);

		rp.append(points.at(i).rp);
	}

	Transform t(rp);
	if (t.isNull()) {
		_errorString = t.errorString();
		return false;
	}

	_transform = t.transform();

	return true;
}

bool MapFile::load(QIODevice &file)
{
	QList<CalibrationPoint> points;
	QString projection, datum;
	Projection::Setup setup;

	if (!parseMapFile(file, points, projection, setup, datum))
		return false;
	if (!createDatum(datum))
		return false;
	if (!createProjection(projection, setup, points))
		return false;
	if (!computeTransformation(points))
		return false;

	return true;
}
